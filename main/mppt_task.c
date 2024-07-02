#include "mppt_task.h"
#include "INA228.h"
#include "driver/gpio.h"
#include "user_data.h"
#include "driver/ledc.h"
#include "modbus.h"

#define MPPT_POWER_LED	27
//#define MPPT_OUTPUT_ON_OFF
#define MPPT_OUTPUT_EN	33
#define MPPT_OUTPUT_PWM	32

#define MPPT_POWER_LED_SEL (1ULL<<MPPT_POWER_LED)
#define MPPT_OUTPUT_EN_SEL	(1ULL<<MPPT_OUTPUT_EN)
#define MPPT_OUTPUT_PWM_SEL	(1ULL<<MPPT_OUTPUT_PWM)

#define MPPT_HS_TIMER          LEDC_TIMER_0
#define MPPT_HS_MODE           LEDC_HIGH_SPEED_MODE
#define MPPT_HS_CH0_GPIO       (32)
#define MPPT_HS_CH0_CHANNEL    LEDC_CHANNEL_0
#define MPPT_CH_NUM			   1

mppt_t gMPPT;
extern uint16_t Test[50];

AutoBuf autoBaud;

/*void init_crc16(void)
{
	CRClo = 0xFF;
	CRChi = 0xFF;
}*/

/*uint16_t crc16(uint8_t *p, uint8_t length)
{
	uint8_t uchCRCHi = 0xff;	// high byte of CRC initialized
	uint8_t uchCRCLo = 0xff;	// low byte of CRC initialized
	uint8_t uIndex;			// will index into CRC lookup table
	uint8_t i = 0;

	while(length--)//pass through message buffer
	{
		uIndex = uchCRCHi^p[i++];		// calculate the CRC
		//uchCRCHi = uchCRCLo^auchCRCHi[uIndex];
		//uchCRCLo = auchCRCLo[uIndex];
	}
	return (((uint16_t)uchCRCHi << 8) | uchCRCLo);
}*/

/**
  * @brief  Function implementing the AUTOBAUD thread.
  * @param  argument: Not used
  * @retval None
  */
/*uint8_t checkAutoBaudData(uint16_t address)
{
	uint16_t crc_val;

//	if(revce_count != rece_size)
//		return 0;

	if(autoBaud.buf[0] != 255 && autoBaud.buf[0] != Modbus.address && autoBaud.buf[0] != 0)
		return 0;


	// check that message is one of the following
	if( (autoBaud.buf[1]!=READ_VARIABLES) && (autoBaud.buf[1]!=WRITE_VARIABLES) && (autoBaud.buf[1]!=MULTIPLE_WRITE) &&( autoBaud.buf[1]!=CHECKONLINE))
		return 0;

	crc_val = crc16(autoBaud.buf, autoBaud.length-2);

	if(crc_val == (autoBaud.buf[autoBaud.length-2]<<8) + autoBaud.buf[autoBaud.length-1] )
	{
		return 1;
	}
	else
	{
		return 0;
	}
 }*/

/* Accumulate "dataValue" into the CRC in crcValue. */
/* Return value is updated CRC */
/* */
/*  The ^ operator means exclusive OR. */
/* Note: This function is copied directly from the BACnet standard. */
/*uint8_t CRC_Calc_Header(
    uint8_t dataValue,
    uint8_t crcValue)
{
    uint16_t crc;

    crc = crcValue ^ dataValue; // XOR C7..C0 with D7..D0

    // Exclusive OR the terms in the table (top down)
    crc = crc ^ (crc << 1) ^ (crc << 2) ^ (crc << 3)
        ^ (crc << 4) ^ (crc << 5) ^ (crc << 6)
        ^ (crc << 7);

    // Combine bits shifted out left hand end
    return (crc & 0xfe) ^ ((crc >> 8) & 1);
}*/

bool CheckBacnetData(uint16_t len)
{
	uint8_t i;
	uint8_t crc8 = 0xFF;        /* used to calculate the crc value */
	//uint16_t crc16 = 0xFFFF;    /* used to calculate the crc value */

	if(len>= 8)
	{
		for(i= 0; i<len; i++)
		{
		 if((autoBaud.buf[i]==0x55)&&(autoBaud.buf[i+1] == 0xff))
		 {
			crc8 = 0xFF;
/*			crc8 = CRC_Calc_Header(autoBaud.buf[i+2], crc8);
			crc8 = CRC_Calc_Header(autoBaud.buf[i+3], crc8);
			crc8 = CRC_Calc_Header(autoBaud.buf[i+4], crc8);
			crc8 = CRC_Calc_Header(autoBaud.buf[i+5], crc8);
			crc8 = CRC_Calc_Header(autoBaud.buf[i+6], crc8);*/
			if(autoBaud.buf[i+7] == (uint8_t)(~crc8))
			//if(autoBaud.buf[i+6] == crc8)
			{
				return true;
			}
		 }
		}
	}
	return false;
}

void mppt_pwm_init(void)
{
	int ch;

	ledc_timer_config_t mppt_pwm_timer = {
		.duty_resolution = LEDC_TIMER_8_BIT, // resolution of PWM duty
		.freq_hz = 10000,                      // frequency of PWM signal
		.speed_mode = MPPT_HS_MODE,           // timer mode
		.timer_num = MPPT_HS_TIMER,            // timer index
		.clk_cfg = LEDC_USE_APB_CLK,//LEDC_AUTO_CLK,              // Auto select the source clock
	};

	ledc_timer_config(&mppt_pwm_timer);
	ledc_channel_config_t mppt_channel[MPPT_CH_NUM] = {
		{
			.channel    = MPPT_HS_CH0_CHANNEL,
			.duty       = 0,
			.gpio_num   = MPPT_HS_CH0_GPIO,
			.speed_mode = MPPT_HS_MODE,
			.hpoint     = 0,
			.timer_sel  = MPPT_HS_TIMER
		},
	};

	for (ch = 0; ch < MPPT_CH_NUM; ch++) {
		ledc_channel_config(&mppt_channel[ch]);
	}

	ledc_set_duty(mppt_channel[0].speed_mode, mppt_channel[0].channel, gMPPT.output_pwm);
	ledc_update_duty(mppt_channel[0].speed_mode, mppt_channel[0].channel);
}

void mppt_task_init(void)
{
	gpio_config_t io_conf;
	//disable interrupt
	io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	//set as output mode
	io_conf.mode = GPIO_MODE_OUTPUT;
	//bit mask of the pins that you want to set
	io_conf.pin_bit_mask = MPPT_POWER_LED_SEL | MPPT_OUTPUT_EN_SEL ;//| MPPT_OUTPUT_PWM_SEL ;
	//disable pull-down mode
	io_conf.pull_down_en = 0;
	//disable pull-up mode
	io_conf.pull_up_en = 0;
	//configure GPIO with the given settings
	gpio_config(&io_conf);

	gpio_set_level(MPPT_POWER_LED, 0);

	ina228_i2c_init();
	ina228_init(I2C_MASTER_NUM, INA228_SLAVE_ADDRESS);
	ina228_init(I2C_MASTER_NUM, INA228_SLAVE_OUTPUT_ADDRESS);

	mppt_pwm_init();

	xTaskCreate(mppt_task, "mppt_task", 2048*2, NULL, 2, NULL);
	xTaskCreate(ina228_read_task, "ina228_read_task", 2048*2, NULL, 2, NULL);
}

void ina228_read_task(void* arg)
{

	while(1)
	{
		inputs[0].value = Test[47] = gMPPT.input_voltage = (uint16_t)(ina228_voltage(I2C_MASTER_NUM, INA228_SLAVE_ADDRESS)*1000);
		inputs[1].value = Test[48] = gMPPT.input_current = (uint16_t)(ina228_current(I2C_MASTER_NUM, INA228_SLAVE_ADDRESS)*1000);
		inputs[2].value = Test[49] = gMPPT.input_power = (uint16_t)(ina228_power(I2C_MASTER_NUM, INA228_SLAVE_ADDRESS)*1000);
		inputs[3].value = Test[46] = gMPPT.input_energy = (int16_t)(ina228_energy(I2C_MASTER_NUM, INA228_SLAVE_ADDRESS)*1000);
		inputs[4].value = Test[40] = gMPPT.output_voltage = (uint16_t)(ina228_voltage(I2C_MASTER_NUM, INA228_SLAVE_OUTPUT_ADDRESS)*1000);
		inputs[5].value = Test[41] = gMPPT.output_current = (uint16_t)(ina228_current(I2C_MASTER_NUM, INA228_SLAVE_OUTPUT_ADDRESS)*1000);
		inputs[6].value = Test[42] = gMPPT.output_power = (uint16_t)(ina228_power(I2C_MASTER_NUM, INA228_SLAVE_OUTPUT_ADDRESS)*1000);
		inputs[7].value = Test[43] = gMPPT.output_energy = (int16_t)(ina228_energy(I2C_MASTER_NUM, INA228_SLAVE_OUTPUT_ADDRESS)*1000);
		//Test[45] = outputs[0].value/1000;

		vTaskDelay(2000 / portTICK_RATE_MS);//pdMS_TO_TICKS(1000));
	}
}

/*void Charging_Algorithm(){
  //if(gMPPT.ERR>0||gMPPT.chargingPause==1){//buck_Disable();
  //}                                       //�����ִ��������ͣ������ͣ����ʱ�ر� MPPT ��ѹ
  //else
	{
//    if(gMPPT.REC==1){                                                                      // IUV RECOVERY - (���Գ��ģʽ��Ч)
//    	gMPPT.REC=0;                                                                         //���� IUV �ָ�������ʶ��
//      buck_Disable();                                                                //�� PPWM ��ʼ��֮ǰ���ý�ѹ
      //lcd.setCursor(0,0);lcd.print("POWER SOURCE    ");                              //��ʾҺ����Ϣ
      //lcd.setCursor(0,1);lcd.print("DETECTED        ");                              //��ʾҺ����Ϣ
      //tft.fillScreen(TFT_BLACK);
      //tft.drawString("POWER SOURCE DETECTED", 10, 40, 3);
      //Serial.println("> Solar Panel Detected");                                      //��ʾ������Ϣ
      //Serial.print("> Computing For Predictive PWM ");                               //��ʾ������Ϣ
//      for(int i = 0; i<40; i++){delay(30);}                        //For loop "loading... Ч��
      //Serial.println("");                                                            //�ڴ�������ʾ��һ�еĻ��з�
//      Read_Sensors();
//      predictivePWM();
//      gMPPT.PWM = gMPPT.PPWM;
      //lcd.clear();
//    }
//    else
		{                                                                            //NO ERROR PRESENT - ������Դת��
      /////////////////////// CC-CV BUCK PSU ALGORITHM //////////////////////////////
      //
      //PSU��cc-cvģʽ�ĳ���㷨���⣬�������趨ֵ������趨ֵ��������Դ�ֵ���ᵼ��pwm�����������������ѹֱ���ﵽС�ڵ�ص�ѹ������
      //����趨��Сֵ�����޷����������

      if(gMPPT.MPPT_Mode==0){                                                              // CC-CV PSU ģʽ
        //if(PSUcurrentMax>=currentCharging || PSUcurrentMax==0.0000 || currentOutput<0.02){PSUcurrentMax = currentCharging;} //��ʼ��psu����������

        if(gMPPT.output_current >gMPPT.currentCharging)     {gMPPT.PWM--;}                             //���������޶�ֵ �� ����ռ�ձ�
		    //psuģʽ��psu���ģʽ����Ҫ����һ�£����ģʽΪ�˳�����ե������Դ��psuģʽ�򲢲�һ����Ҫ��������ʱ�رմ��ж� 20220811
        //if(currentOutput>PSUcurrentMax)       {PWM--;}                               //���������ⲿ���ֵ �� ����ռ�ձ�
        else if(gMPPT.output_voltage>gMPPT.voltageBatteryMax){gMPPT.PWM--;}                             //��ѹ���� �� ����ռ�ձ�
        else if(gMPPT.output_voltage<gMPPT.voltageBatteryMax){gMPPT.PWM++;}                             //��������ڳ���ѹʱ����ռ�ձȣ������� CC-CV ģʽ��
        else{}                                                                       //���ﵽ�趨�������ѹʱʲô������
        //PWM_Modulation();                                                            //�� PWM �ź�����Ϊ Buck PWM GPIO
        gMPPT.voltageInputPrev = gMPPT.input_voltage;                                             //	�洢��ǰ��¼�ĵ�ѹ
      }
      ///////////////////////  MPPT & CC-CV ����㷨 ///////////////////////  mpptģʽֻҪ��ֹ��ѹ���͵���ص�ѹ�����ٽ��������Ķ��ද��
      else if(gMPPT.MPPT_Mode == 1){
        if(gMPPT.output_current>gMPPT.currentCharging){gMPPT.PWM--;}                                         //�������� �� ����ռ�ձ�
        else if(gMPPT.output_current>gMPPT.voltageBatteryMax){gMPPT.PWM--;}                                  //��ѹ���� �� ����ռ�ձ�
        else{                                                                             //MPPT �㷨
          if( gMPPT.output_current>0.1 && gMPPT.input_voltage>=(gMPPT.output_voltage+gMPPT.voltageDropout+1)){       //�޷��������������ڵ�ص�ѹ�������½���pwm������ֹ�������͵�ѹ	20220803
            if(gMPPT.input_power>gMPPT.powerInputPrev && gMPPT.input_voltage>gMPPT.voltageInputPrev)     {gMPPT.PWM--;}   //  ��P ��V ; ��MPP //D-- 	���������ҵ�ѹ���������� ̧�ߵ�ѹ
            else if(gMPPT.input_power>gMPPT.powerInputPrev && gMPPT.input_voltage<gMPPT.voltageInputPrev){gMPPT.PWM++;}   //  ��P ��V ; MPP�� //D++	���������ҵ�ѹ���ͣ����� ���͵�ѹ
            else if(gMPPT.input_power<gMPPT.powerInputPrev && gMPPT.input_voltage>gMPPT.voltageInputPrev){gMPPT.PWM++;}   //  ��P ��V ; MPP�� //D++	�����½�����ѹ���������� ���͵�ѹ
            else if(gMPPT.input_power<gMPPT.powerInputPrev && gMPPT.input_voltage<gMPPT.voltageInputPrev){gMPPT.PWM--;}   //  ��P ��V ; ��MPP  //D--	�����½�����ѹ�½������� ̧�ߵ�ѹ
            else if(gMPPT.output_voltage>gMPPT.voltageBatteryMax)                           {gMPPT.PWM--;}   //  MP MV ; �ﵽ MPP
            else if(gMPPT.output_voltage<gMPPT.voltageBatteryMax)                           {gMPPT.PWM++;}   //  MP MV ; �ﵽ MPP
          }else{
        	  gMPPT.PWM--;
          }

          if(gMPPT.output_current<=0){gMPPT.PWM=gMPPT.PWM+2;}  //���������ֵ
          gMPPT.powerInputPrev   = gMPPT.input_power;                                               //  �洢��ǰ��¼�Ĺ���
          gMPPT.voltageInputPrev = gMPPT.input_voltage;                                             //	�洢��ǰ��¼�ĵ�ѹ
        }
//        PWM_Modulation();                                                              //�� PWM �ź�����Ϊ Buck PWM GPIO
      }
    }
  }
}*/

void mppt_task(void* arg)
{
	static bool power_led_trigger=0;

	//mppt_pwm_init();
	gMPPT.output_pwm = 127;
	gpio_set_level(MPPT_OUTPUT_EN, 1);
	//gpio_set_level(MPPT_OUTPUT_PWM, 1);
	gpio_set_level(MPPT_POWER_LED, 1);
    while (1) {

    	/*if(power_led_trigger){
    		gpio_set_level(MPPT_POWER_LED, 0);
    		if(Test[44] == 1)
    			gpio_set_level(MPPT_OUTPUT_EN, 1);
    		else
    			gpio_set_level(MPPT_OUTPUT_EN, 0);
    		if(Test[45] == 1)
    			gpio_set_level(MPPT_OUTPUT_PWM, 1);
    		else
    			gpio_set_level(MPPT_OUTPUT_PWM, 1);
    	}
    	else{
    		gpio_set_level(MPPT_OUTPUT_PWM, 1);
    		if(Test[44] == 1)
    			gpio_set_level(MPPT_OUTPUT_EN, 1);
    		else
    			gpio_set_level(MPPT_OUTPUT_EN, 0);
    		if(Test[45] == 1)
    			gpio_set_level(MPPT_OUTPUT_PWM, 0);
    		else
    			gpio_set_level(MPPT_OUTPUT_PWM, 1);
    	}
    	power_led_trigger= !power_led_trigger;*/


    	gMPPT.MPPT_Mode = Test[45];

		gMPPT.output_pwm = controllers[1].value; // use pid to control output pwm.
		if(gMPPT.input_voltage < gMPPT.output_voltage)
			gMPPT.output_pwm++;
		else if(gMPPT.input_voltage > gMPPT.output_voltage)
			gMPPT.output_pwm--;
		else
		{
			//gMPPT.output_pwm=gMPPT.output_pwm;
		}

		//Charging_Algorithm();

		//ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, gMPPT.output_pwm);
		//ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, 100);
		//delay_ms(10);
		//ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, 50);
		//delay_ms(10);
		//ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, 200);
		//delay_ms(10);
		//ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, 100);
		//delay_ms(10);
		//ledc_update_duty(mppt_channel[0].speed_mode, mppt_channel[0].channel);
		//ledc_update_duty(mppt_channel[0].speed_mode, mppt_channel[0].channel);

		//ledc_set_duty(MPPT_HS_MODE, MPPT_HS_CH0_CHANNEL, Test[44]);//gMPPT.output_pwm);

		if(gMPPT.MPPT_Mode == 2)  //PID mode or manual mode
		{
			Test[44] = (outputs[0].value/1000)*125/100;
			ledc_set_duty(MPPT_HS_MODE, MPPT_HS_CH0_CHANNEL, (outputs[0].value/1000)*125/100);//gMPPT.output_pwm);
			ledc_update_duty(MPPT_HS_MODE, MPPT_HS_CH0_CHANNEL);
		}




        vTaskDelay(300 / portTICK_RATE_MS);//pdMS_TO_TICKS(1000));
    }
}

/*
void PWM_Modulation(){
  if(output_Mode==0){PWM = constrain(PWM,0,pwmMaxLimited);}                          //PSU MODE PWM = PWM OVERFLOW PROTECTION������������Ϊ 0%����������Ϊ�������ռ�ձȣ�
  else{
    predictivePWM();                                                                 //���кͼ���Ԥ�� pwm floor
    PWM = constrain(PWM,PPWM,pwmMaxLimited);                                         //CHARGER MODE PWM - ����������Ϊ PPWM����������Ϊ�������ռ�ձȣ�
  }
  ledcWrite(pwmChannel,PWM);                                                         //���� PWM ռ�ձȲ������ý�ѹʱд�� GPIO
  buck_Enable();                                                                     //���� MPPT ��ѹ (IR2104)
}*/
#if 0

void Charging_Algorithm(){
  if(gMPPT.ERR>0||gMPPT.chargingPause==1){//buck_Disable();
  }                                       //�����ִ��������ͣ������ͣ����ʱ�ر� MPPT ��ѹ
  else{
    if(gMPPT.REC==1){                                                                      // IUV RECOVERY - (���Գ��ģʽ��Ч)
    	gMPPT.REC=0;                                                                         //���� IUV �ָ�������ʶ��
//      buck_Disable();                                                                //�� PPWM ��ʼ��֮ǰ���ý�ѹ
      //lcd.setCursor(0,0);lcd.print("POWER SOURCE    ");                              //��ʾҺ����Ϣ
      //lcd.setCursor(0,1);lcd.print("DETECTED        ");                              //��ʾҺ����Ϣ
      //tft.fillScreen(TFT_BLACK);
      //tft.drawString("POWER SOURCE DETECTED", 10, 40, 3);
      //Serial.println("> Solar Panel Detected");                                      //��ʾ������Ϣ
      //Serial.print("> Computing For Predictive PWM ");                               //��ʾ������Ϣ
//      for(int i = 0; i<40; i++){delay(30);}                        //For loop "loading... Ч��
      //Serial.println("");                                                            //�ڴ�������ʾ��һ�еĻ��з�
//      Read_Sensors();
//      predictivePWM();
      gMPPT.PWM = gMPPT.PPWM;
      //lcd.clear();
    }
    else{                                                                            //NO ERROR PRESENT - ������Դת��
      /////////////////////// CC-CV BUCK PSU ALGORITHM //////////////////////////////
      /*
      PSU��cc-cvģʽ�ĳ���㷨���⣬�������趨ֵ������趨ֵ��������Դ�ֵ���ᵼ��pwm�����������������ѹֱ���ﵽС�ڵ�ص�ѹ������
      ����趨��Сֵ�����޷����������
      */
      if(gMPPT.MPPT_Mode==0){                                                              // CC-CV PSU ģʽ
        //if(PSUcurrentMax>=currentCharging || PSUcurrentMax==0.0000 || currentOutput<0.02){PSUcurrentMax = currentCharging;} //��ʼ��psu����������

        if(gMPPT.output_current >gMPPT.currentCharging)     {gMPPT.PWM--;}                             //���������޶�ֵ �� ����ռ�ձ�
		    //psuģʽ��psu���ģʽ����Ҫ����һ�£����ģʽΪ�˳�����ե������Դ��psuģʽ�򲢲�һ����Ҫ��������ʱ�رմ��ж� 20220811
        //if(currentOutput>PSUcurrentMax)       {PWM--;}                               //���������ⲿ���ֵ �� ����ռ�ձ�
        else if(gMPPT.output_voltage>gMPPT.voltageBatteryMax){gMPPT.PWM--;}                             //��ѹ���� �� ����ռ�ձ�
        else if(gMPPT.output_voltage<gMPPT.voltageBatteryMax){gMPPT.PWM++;}                             //��������ڳ���ѹʱ����ռ�ձȣ������� CC-CV ģʽ��
        else{}                                                                       //���ﵽ�趨�������ѹʱʲô������
        //PWM_Modulation();                                                            //�� PWM �ź�����Ϊ Buck PWM GPIO
        gMPPT.voltageInputPrev = gMPPT.input_voltage;                                             //	�洢��ǰ��¼�ĵ�ѹ
      }
      ///////////////////////  MPPT & CC-CV ����㷨 ///////////////////////  mpptģʽֻҪ��ֹ��ѹ���͵���ص�ѹ�����ٽ��������Ķ��ද��
      else{
        if(gMPPT.output_current>gMPPT.currentCharging){gMPPT.PWM--;}                                         //�������� �� ����ռ�ձ�
        else if(gMPPT.output_current>gMPPT.voltageBatteryMax){gMPPT.PWM--;}                                  //��ѹ���� �� ����ռ�ձ�
        else{                                                                             //MPPT �㷨
          if( gMPPT.output_current>0.1 && gMPPT.input_voltage>=(gMPPT.output_voltage+gMPPT.voltageDropout+1)){       //�޷��������������ڵ�ص�ѹ�������½���pwm������ֹ�������͵�ѹ	20220803
            if(gMPPT.input_power>gMPPT.powerInputPrev && gMPPT.input_voltage>gMPPT.voltageInputPrev)     {gMPPT.PWM--;}   //  ��P ��V ; ��MPP //D-- 	���������ҵ�ѹ���������� ̧�ߵ�ѹ
            else if(gMPPT.input_power>gMPPT.powerInputPrev && gMPPT.input_voltage<gMPPT.voltageInputPrev){gMPPT.PWM++;}   //  ��P ��V ; MPP�� //D++	���������ҵ�ѹ���ͣ����� ���͵�ѹ
            else if(gMPPT.input_power<gMPPT.powerInputPrev && gMPPT.input_voltage>gMPPT.voltageInputPrev){gMPPT.PWM++;}   //  ��P ��V ; MPP�� //D++	�����½�����ѹ���������� ���͵�ѹ
            else if(gMPPT.input_power<gMPPT.powerInputPrev && gMPPT.input_voltage<gMPPT.voltageInputPrev){gMPPT.PWM--;}   //  ��P ��V ; ��MPP  //D--	�����½�����ѹ�½������� ̧�ߵ�ѹ
            else if(gMPPT.output_voltage>gMPPT.voltageBatteryMax)                           {gMPPT.PWM--;}   //  MP MV ; �ﵽ MPP
            else if(gMPPT.output_voltage<gMPPT.voltageBatteryMax)                           {gMPPT.PWM++;}   //  MP MV ; �ﵽ MPP
          }else{
        	  gMPPT.PWM--;
          }

          if(gMPPT.output_current<=0){gMPPT.PWM=gMPPT.PWM+2;}  //���������ֵ
          gMPPT.powerInputPrev   = gMPPT.input_power;                                               //  �洢��ǰ��¼�Ĺ���
          gMPPT.voltageInputPrev = gMPPT.input_voltage;                                             //	�洢��ǰ��¼�ĵ�ѹ
        }
//        PWM_Modulation();                                                              //�� PWM �ź�����Ϊ Buck PWM GPIO
      }
    }
  }
}
#endif

