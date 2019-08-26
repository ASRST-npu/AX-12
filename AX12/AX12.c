/*

Code to control multiple Dynamixel AX-12 servo motors over USART
on an STM32F4 chip.

Kent deVillafranca
April 2013

*/

#include "AX12.h"
#include "delay.h"
#include "LED.h"
#include "usart.h"


#define SERVO_DEBUG
//__asm__(".global _printf_float");
//__asm__(".global _scanf_float"); 
#define getServoBytesAvailable_TEST1


#ifndef true
#define true ((bool)1)
#endif	

#ifndef false
#define false ((bool)0)
#endif

uint8_t servoErrorCode = 0;


ServoResponse response;


volatile uint8_t receiveBuffer[REC_BUFFER_LEN];
volatile uint8_t* volatile receiveBufferStart = receiveBuffer;
volatile uint8_t* volatile receiveBufferEnd = receiveBuffer;


#define RETURN_DELAY        0x05
#define BLINK_CONDITIONS    0x11
#define SHUTDOWN_CONDITIONS 0x12
#define TORQUE              0x22
#define MAX_SPEED           0x20
#define CURRENT_SPEED       0x26
#define GOAL_ANGLE          0x1e
#define CURRENT_ANGLE       0x24

void sendServoCommand (const uint8_t servoId,
                       const ServoCommand commandByte,
                       const uint8_t numParams,
                       const uint8_t *params)
{
	uint8_t checksum = servoId;
	uint8_t i = 0;
	
    sendServoByte (0xff);
    sendServoByte (0xff);  // command header
    
    sendServoByte (servoId);  // servo ID
    
    
    sendServoByte (numParams + 2);  // number of following bytes
    sendServoByte ((uint8_t)commandByte);  // command
    
    checksum += numParams + 2 + commandByte;
    
    for (i = 0; i < numParams; i++)
    {
        sendServoByte (params[i]);  // parameters
        checksum += params[i];
    }
    
    sendServoByte (~checksum);  // checksum
}

bool getServoResponse (void)
{
    uint8_t retries = 0;
	uint8_t i = 0;
    uint8_t calcChecksum = response.id + response.length + response.error;
	uint8_t recChecksum = getServoByte();
	
    clearServoReceiveBuffer();
    
    while (getServoBytesAvailable() < 4)
    {
        retries++;
        if (retries > REC_WAIT_MAX_RETRIES)
        {
            #ifdef SERVO_DEBUG
            printf ("Too many retries at start\n");
            #endif
            return false;
        }
        
        delay_us(REC_WAIT_START_US);
    }
    retries = 0;
    
    getServoByte();  // servo header (two 0xff bytes)
    getServoByte();
    
    response.id = getServoByte();
    response.length = getServoByte();
    
    if (response.length > SERVO_MAX_PARAMS)
    {
        #ifdef SERVO_DEBUG
        printf ("Response length too big: %d\n", (int)response.length);
        #endif
        return false;
    }
    
    while (getServoBytesAvailable() < response.length)
    {
        retries++;
        if (retries > REC_WAIT_MAX_RETRIES)
        {
            #ifdef SERVO_DEBUG
            printf ("Too many retries waiting for params, got %d of %d params\n", getServoBytesAvailable(), response.length);
            #endif
            return false;
        }
        
        delay_us(REC_WAIT_PARAMS_US);
    }
    
    response.error = getServoByte();
    servoErrorCode = response.error;
    
    for (i = 0; i < response.length - 2; i++)
        response.params[i] = getServoByte();
    
   
    for (i = 0; i < response.length - 2; i++)
        calcChecksum += response.params[i];
    calcChecksum = ~calcChecksum;
    

    if (calcChecksum != recChecksum)
    {
        #ifdef SERVO_DEBUG
        printf ("Checksum mismatch: %x calculated, %x received\n", calcChecksum, recChecksum);
        #endif
        return false;
    }
    
    return true;
}

__inline bool getAndCheckResponse (const uint8_t servoId)
{
    if (!getServoResponse())
    {
        #ifdef SERVO_DEBUG
        printf ("Servo error: Servo %d did not respond correctly or at all\n", (int)servoId);
        #endif
        return false;
    }
    
    if (response.id != servoId)
    {
        #ifdef SERVO_DEBUG
        printf ("Servo error: Response ID %d does not match command ID %d\n", (int)response.id,(int)response.id);
        #endif
        return false;
    }
    
    if (response.error != 0)
    {
        #ifdef SERVO_DEBUG
        printf ("Servo error: Response error code was nonzero (%d)\n", (int)response.error);
        #endif
        return false;
    }
    
    return true;
}

// ping a servo, returns true if we get back the expected values
bool pingServo (const uint8_t servoId)
{
    sendServoCommand (servoId, PING , 0, 0);
    if (!getAndCheckResponse (servoId))
        return false;
    
    return true;
}

bool setServoReturnDelayMicros (const uint8_t servoId, const uint16_t micros)
{

	uint8_t params[2];
    params[0] = RETURN_DELAY;
	params[1] = (uint8_t)((micros / 2) & 0xff);
	
	if (micros > 510)
        return false;
    
    sendServoCommand (servoId, WRITE, 2, params);
    
    if (!getAndCheckResponse (servoId))
        return false;
    
    return true;
}

// set the events that will cause the servo to blink its LED
bool setServoBlinkConditions (const uint8_t servoId, const uint8_t flags)
{
	uint8_t params[2];
    params[0] = BLINK_CONDITIONS;
	params[1] = flags;
	
    sendServoCommand (servoId, WRITE, 2, params);
    
    if (!getAndCheckResponse (servoId))
        return false;
    
    return true;
}

// set the events that will cause the servo to shut off torque
bool setServoShutdownConditions (const uint8_t servoId,
                                 const uint8_t flags)
{
    uint8_t params[2];
    params[0] = SHUTDOWN_CONDITIONS;
	params[1] = flags;
	
    sendServoCommand (servoId, WRITE, 2, params);
    
    if (!getAndCheckResponse (servoId))
        return false;
    
    return true;
}


// valid torque values are from 0 (free running) to 1023 (max)
bool setServoTorque (const uint8_t servoId,
                     const uint16_t torqueValue)
{
    const uint8_t highByte = (uint8_t)((torqueValue >> 8) & 0xff);
    const uint8_t lowByte = (uint8_t)(torqueValue & 0xff);
    uint8_t params[3];
	
    params[0] = TORQUE;
	params[1] = lowByte;
	params[2] = highByte;
	
    if (torqueValue > 1023)
        return false;
    
    sendServoCommand (servoId, WRITE, 3, params);
    
    if (!getAndCheckResponse (servoId))
        return false;
    
    return true;
}

bool getServoTorque (const uint8_t servoId, uint16_t *torqueValue)
{
    const uint8_t params[2] = {TORQUE,2};  // read two bytes, starting at address TORQUE
    
    sendServoCommand (servoId, READ, 2, params);
    
    if (!getAndCheckResponse (servoId))
        return false;
    
    *torqueValue = response.params[1];
    *torqueValue <<= 8;
    *torqueValue |= response.params[0];
    
    return true;
}

// speed values go from 1 (incredibly slow) to 1023 (114 RPM)
// a value of zero will disable velocity control
bool setServoMaxSpeed (const uint8_t servoId, const uint16_t speedValue)
{
    const uint8_t highByte = (uint8_t)((speedValue >> 8) & 0xff);
    const uint8_t lowByte = (uint8_t)(speedValue & 0xff);
    uint8_t params[3];
	
	params[0] = MAX_SPEED;
	params[1] = lowByte;
	params[2] = highByte;
		
    if (speedValue > 1023)
        return false;
    
    sendServoCommand (servoId, WRITE, 3, params);
    
    if (!getAndCheckResponse (servoId))
        return false;
    
    return true;
}

bool getServoMaxSpeed (const uint8_t servoId, uint16_t *speedValue)
{
    const uint8_t params[2] = {MAX_SPEED, 2};  // read two bytes, starting at address MAX_SPEED
    
    sendServoCommand (servoId, READ, 2, params);
    
    if (!getAndCheckResponse (servoId))
        return false;
    
    *speedValue = response.params[1];
    *speedValue <<= 8;
    *speedValue |= response.params[0];
    
    return true;
}

bool getServoCurrentVelocity (const uint8_t servoId, int16_t *velocityValue)
{
    const uint8_t params[2] = {CURRENT_SPEED, 2};  // read two bytes, starting at address CURRENT_SPEED
    
    sendServoCommand (servoId, READ, 2, params);
    
    if (!getAndCheckResponse (servoId))
        return false;
    
    *velocityValue = response.params[1];
    *velocityValue <<= 8;
    *velocityValue |= response.params[0];
    
    return true;
}

// make the servo move to an angle
// valid angles are between 0 and 300 degrees
bool setServoAngle (const uint8_t servoId,
                    const float angle)
{
	// angle values go from 0 to 0x3ff (1023)
    const uint16_t angleValue = (uint16_t)(angle * (1023 / 300));
    const uint8_t highByte = (uint8_t)((angleValue >> 8) & 0xff);
    const uint8_t lowByte = (uint8_t)(angleValue & 0xff);
	uint8_t params[3];
	
	params[0] = GOAL_ANGLE;
	params[1] = lowByte;
	params[2] = highByte;
	
    if (angle < 0 || angle > 300)
        return false;

	
    sendServoCommand (servoId, WRITE, 3, params);
    
    if (!getAndCheckResponse (servoId))
        return false;
    
    return true;
}

bool getServoAngle (const uint8_t servoId,
                    float *angle)
{
    const uint8_t params[2] = {CURRENT_ANGLE, 2};  // read two bytes, starting at address CURRENT_ANGLE
    uint16_t angleValue = response.params[1];
	
    sendServoCommand (servoId, READ, 2, params);
    
    if (!getAndCheckResponse (servoId))
        return false;
    

    angleValue <<= 8;
    angleValue |= response.params[0];
    
    *angle = (float)angleValue * 300 / 1023;
    
    return true;
}
   	 

void ServoUSART_Init(u32 bound){
   //GPIO�˿�����
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD,ENABLE); //ʹ��GPIODʱ��
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3,ENABLE);//ʹ��USART3ʱ��
 
	//����1��Ӧ���Ÿ���ӳ��
	GPIO_PinAFConfig(GPIOD,GPIO_PinSource9,GPIO_AF_USART3); //GPIOD9����ΪUSART3
	GPIO_PinAFConfig(GPIOD,GPIO_PinSource8,GPIO_AF_USART3); //GPIOD8����ΪUSART3
	
	//USART1�˿�����
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9; //GPIOD8��GPIOD9
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;//���ù���
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;	//�ٶ�50MHz
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP; //���츴�����
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP; //����
	GPIO_Init(GPIOD,&GPIO_InitStructure); //��ʼ��PD8��PD9

   //USART1 ��ʼ������
	USART_InitStructure.USART_BaudRate = bound;//����������
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;//�ֳ�Ϊ8λ���ݸ�ʽ
	USART_InitStructure.USART_StopBits = USART_StopBits_1;//һ��ֹͣλ
	USART_InitStructure.USART_Parity = USART_Parity_No;//����żУ��λ
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;//��Ӳ������������
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;	//�շ�ģʽ
	USART_Init(USART3, &USART_InitStructure); //��ʼ������3
	
	USART_HalfDuplexCmd (USART3, ENABLE);
	USART_Cmd(USART3, ENABLE);  //ʹ�ܴ���3
	
	//USART_ClearFlag(USART3, USART_FLAG_TC);
		
	USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);//��������ж�

	//Usart3 NVIC ����
	NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;//����3�ж�ͨ��
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=0;//��ռ���ȼ�3
	NVIC_InitStructure.NVIC_IRQChannelSubPriority =0;		//�����ȼ�3
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			//IRQͨ��ʹ��
	NVIC_Init(&NVIC_InitStructure);	//����ָ���Ĳ�����ʼ��VIC�Ĵ�����
	USART_ITConfig(USART3,USART_IT_ORE,ENABLE);
	
//	USART3_RX_STA=0;		//����
		
}


void USART3_IRQHandler (void)
{
	uint16_t byte;
	// check if the USART3 receive interrupt flag was set
	if (USART_GetITStatus (USART3, USART_IT_RXNE))
	{
		byte = USART_ReceiveData (USART3); // grab the byte from the data register
        
        receiveBufferEnd++;
        if (receiveBufferEnd >= receiveBuffer + REC_BUFFER_LEN)
            receiveBufferEnd = receiveBuffer;
        
        *receiveBufferEnd = byte;
	}
}

void sendServoByte (uint16_t byte)
{
	  USART_SendData (USART3, byte);
	  
	  //Loop until the end of transmission
	  while (USART_GetFlagStatus (USART3, USART_FLAG_TC) == RESET);
}

void clearServoReceiveBuffer (void)
{
    receiveBufferStart = receiveBufferEnd;
}

size_t getServoBytesAvailable (void)
{
    volatile uint8_t *start = receiveBufferStart;
    volatile uint8_t *end = receiveBufferEnd;
    
	
    if (end >= start)	
	{
//		#ifdef getServoBytesAvailable_TEST1
////		printf("getServoBytesAvailable_TEST1_1\n\t");
//		printf("end == %d",*end);
//		printf("start == %d",*start);
        return (size_t)(end - start);
		
//		#endif
	}
    else
	{
//		#ifdef getServoBytesAvailable_TEST1
//		printf("getServoBytesAvailable_TEST1_2\n\t");
        return (size_t)(REC_BUFFER_LEN - (start - end));
//		#endif
	}
}

uint8_t getServoByte (void)
{
    receiveBufferStart++;
    if (receiveBufferStart >= receiveBuffer + REC_BUFFER_LEN)
        receiveBufferStart = receiveBuffer;
    
    return *receiveBufferStart;
}

void error (void)
{
    fflush (stdout);
    
    for (;;)
    {
        LED1=!LED1;
        delay_ms (100);
    }
}




