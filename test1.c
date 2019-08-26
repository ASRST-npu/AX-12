#include "test1.h"
#include "AX12.h"
#include "delay.h"
#include "usart.h"
#include "sys.h"
#include "LED.h"

#include "math.h"

extern uint8_t id;

void Test1()
{
	u8 freq = 5;
	u8 t=0;
	
	setServoMaxSpeed(id,0x3ff);
	while(1)
	{
		t=t+0.01;
		setServoAngle(id,100*(sin(6.28*freq*t)+0.5));
	}
	
}


