/*
   ws2812x_dma_timer.ino - DMA/TIMER based ws281x/neopixel routines

   Generate a ws281x compatible pulse train using PWM timer and DMA
   provided duty cycle data.  Connect pin PA1 to the DIN of a ws281x
   led strip and watch the animation.

   2018-07-07 - RRK simplified using RGB values, compute T0/T1 based on F_CPU
   2018-07-05 - RRK reworked to avoid interrupt
*/

#include <libmaple/dma.h>

#define LED_CNT 4 /* I'm using a 4 pixel led strip, change to suit */

#if 0
// you can pick an alternate WS281X pin (default is PA1)
#elif 0
static const int WS281X_PIN = PB0;
#define DMA_REQ_SRC_TIMX_CHNX DMA_REQ_SRC_TIM3_CH3
#define TIMER_DMA_BASE_CCRX TIMER_DMA_BASE_CCR3
#define DMA_CHX DMA_CH2

#elif 0
static const int WS281X_PIN = PB1;
#define DMA_REQ_SRC_TIMX_CHNX DMA_REQ_SRC_TIM3_CH4
#define TIMER_DMA_BASE_CCRX TIMER_DMA_BASE_CCR4
#define DMA_CHX DMA_CH3

#elif 0
static const int WS281X_PIN = PB7;
#define DMA_REQ_SRC_TIMX_CHNX DMA_REQ_SRC_TIM4_CH2
#define TIMER_DMA_BASE_CCRX TIMER_DMA_BASE_CCR2
#define DMA_CHX DMA_CH4

#elif 1
static const int WS281X_PIN = PA1;
#define DMA_REQ_SRC_TIMX_CHNX DMA_REQ_SRC_TIM2_CH2
#define TIMER_DMA_BASE_CCRX TIMER_DMA_BASE_CCR2
#define DMA_CHX DMA_CH7

#elif 0
static const int WS281X_PIN = PA8;
#define DMA_REQ_SRC_TIMX_CHNX DMA_REQ_SRC_TIM1_CH1
#define TIMER_DMA_BASE_CCRX TIMER_DMA_BASE_CCR1
#define DMA_CHX DMA_CH2

#endif

// helpers
#define sizeofs(a) (sizeof(a)/sizeof(a[0]))
#define RGB(r,g,b) g,r,b

#define T800 (F_CPU/800000)
#define TT (T800-1)                    /* ~1.25us */
#define T1 (uint8_t)((float)T800*.56f) /* ~700ns  */
#define T0 (uint8_t)((float)T800*.28f) /* ~350ns  */

#define DMA_BUF_SIZE (LED_CNT*24)+1  /* one zero entry so the PWM pin stays low at DMA completion */

//----------------------------------------------------------------------
// global data

timer_dev *ws281x_timer = PIN_MAP[WS281X_PIN].timer_device;
uint8_t dma_buffer[DMA_BUF_SIZE];

dma_tube_config dma_cfg = {
  .tube_src = dma_buffer,
  .tube_src_size = DMA_SIZE_8BITS,
  .tube_dst = &(ws281x_timer->regs.gen->DMAR),
  .tube_dst_size = DMA_SIZE_16BITS,
  .tube_nr_xfers = DMA_BUF_SIZE,
  .tube_flags = DMA_CFG_SRC_INC /* | DMA_CFG_CIRC | DMA_CFG_CMPLT_IE */,
  .target_data = 0,
  .tube_req_src = DMA_REQ_SRC_TIMX_CHNX
};

//----------------------------------------------------------------------
//

void setup() {
  pinMode(WS281X_PIN, PWM);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  dma_conf();
  timer_conf();
}

void loop() {
  // animation frames - marching B,W,R,G colors
  static const uint8_t frames[][LED_CNT * 3] = {
    { RGB(0, 0, 0x1f), RGB(0x1f, 0x1f, 0x1f), RGB(0x1f, 0, 0), RGB(0, 0x1f, 0) },
    { RGB(0x1f, 0x1f, 0x1f), RGB(0x1f, 0, 0), RGB(0, 0x1f, 0), RGB(0, 0, 0x1f) },
    { RGB(0x1f, 0, 0), RGB(0, 0x1f, 0), RGB(0, 0, 0x1f), RGB(0x1f, 0x1f, 0x1f) },
    { RGB(0, 0x1f, 0), RGB(0, 0, 0x1f), RGB(0x1f, 0x1f, 0x1f), RGB(0x1f, 0, 0) }
  };

  unsigned indx = 0;
  while (indx < sizeofs(frames)) {
    init_dma_buffer(frames[indx++], dma_buffer, LED_CNT * 3);

    dma_start();

    // wait for DMA completion flag or abort after 100msec timeout
    // this normally should take less than 150 usecs for 4 leds
    const uint32_t m = millis();
    while ( !(dma_get_isr_bits(DMA1, DMA_CHX) & DMA_ISR_TCIF1) ) {
      if ((millis() - m) > 100) {
        break;
      }
    }

    on_dma_complete();

    delay(250);
  }
}

//----------------------------------------------------------------------
//

void init_dma_buffer(const uint8_t * const src_pixel_data, uint8_t * dst_pulse_data, unsigned cnt)
{
  unsigned bufindx = 0;

  for (unsigned led = 0; led < cnt ; ++led) {
    dst_pulse_data[bufindx++] = (src_pixel_data[led] & (1 << 7)) ? T1 : T0;
    dst_pulse_data[bufindx++] = (src_pixel_data[led] & (1 << 6)) ? T1 : T0;
    dst_pulse_data[bufindx++] = (src_pixel_data[led] & (1 << 5)) ? T1 : T0;
    dst_pulse_data[bufindx++] = (src_pixel_data[led] & (1 << 4)) ? T1 : T0;
    dst_pulse_data[bufindx++] = (src_pixel_data[led] & (1 << 3)) ? T1 : T0;
    dst_pulse_data[bufindx++] = (src_pixel_data[led] & (1 << 2)) ? T1 : T0;
    dst_pulse_data[bufindx++] = (src_pixel_data[led] & (1 << 1)) ? T1 : T0;
    dst_pulse_data[bufindx++] = (src_pixel_data[led] & (1 << 0)) ? T1 : T0;
  }
}

//----------------------------------------------------------------------
//

void dma_conf()
{
  dma_init(DMA1);
}

void dma_start()
{
  // dma_tube_cfg() - disables DMA, clears ISR flags and sets DMA src,dest,and count

  if ( dma_tube_cfg(DMA1, DMA_CHX, &dma_cfg) != DMA_TUBE_CFG_SUCCESS) {
    while (1); // trap DMA config error here
  }

  dma_enable(DMA1, DMA_CHX);
  timer_resume(ws281x_timer);
}

void on_dma_complete()
{
  timer_pause(ws281x_timer);
  dma_disable(DMA1, DMA_CHX);

  GPIOC->regs->ODR ^= 1 << 13; // toggle builtin_led
}

//----------------------------------------------------------------------
//

void timer_conf()
{
  timer_dma_set_base_addr(ws281x_timer, TIMER_DMA_BASE_CCRX);
  timer_dma_set_burst_len(ws281x_timer, 1);
  timer_dma_enable_req(ws281x_timer, PIN_MAP[WS281X_PIN].timer_channel);
  timer_set_prescaler(ws281x_timer, 0);
  timer_set_reload(ws281x_timer, TT);    // 72000000/800000-1 == 800k period
}
