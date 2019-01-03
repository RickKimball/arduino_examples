/*
   composite_serial - bluepill composite serial device with reset support

   The 
   tested using "Serial" upload method which doesn't configure builtin usb serial
   This code allows you to leave the BOOT0 pin tied high and toggle the reset button to
   upload via the USART0 (PA9/PA10) on /dev/ttyUSB0, then connect to /dev/ttyACM0 
   with putty without having to mess with the BOOT0 pin during development.
   The USBCompositeSerial object normally doesn't toggle the PA12 pin.  
   This behaviour is bluepill specific. (proper bluepills only have a 1k5 pull up on PA12)
   
   Compatible with the libmaple/stm32duino core @ https://github.com/rogerclarkmelbourne/Arduino_STM32/
*/

#include <USBComposite.h>
#include <Streaming.h>

USBCompositeSerial SerialUSB;
uint32_t counter = 0;

void setup()
{
  // Toggle USBD+ (PA12 pin) to signal host to
  // reenumerate our USB device on reset press
  pinMode(PA12, OUTPUT_OPEN_DRAIN); // use external 1k5 resistor
  digitalWrite(PA12, LOW);  // force low to signal reset
  delay(50);
  digitalWrite(PA12, HIGH); // actually floats high, not driven

  SerialUSB.registerComponent();
  USBComposite.begin();
  delay(100);
}

void loop()
{
  // only output data if someone is listening
  if ( SerialUSB.isConnected() ) {
    SerialUSB << "Counter: " << counter << " - Hello World!\n";
  }
  counter++;
  delay(500);
}
