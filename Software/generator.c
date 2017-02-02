//Генератор стандартных сигналов на ATmega48

#include <mega48p.h>
#include <delay.h>

#define ADC_VREF_TYPE 0x40

#define SETBIT(x,y) x=x|(1<<y)    //x - в какой переменной, у - какой бит
#define CLIBIT(x,y) x=x&(~(1<<y)) //x - в какой переменной, у - какой бит

//....................................For AD9833....................................
#define AD_B28      13
#define AD_FSELECT  11
#define AD_RESET     8
#define AD_SLEEP1    7
#define AD_SLEEP12   6
#define AD_OPBITEN   5
#define AD_DIV2      3
#define AD_MODE      1
                                          
//.....................................For SPI......................................
#define SPI_SS       PORTB.2
#define SPI_MOSI     PORTB.3
#define SPI_SCK      PORTB.5 

#define MEA_OUT      PORTB.1
//................................For user interface................................

#define MODE_OFF     0
#define MODE_SIN     1
#define MODE_TRE     2
#define MODE_MEA     3

#define KEY_PLUS     PINB.0
#define KEY_MINUS    PINB.4
#define KEY_MODE     PINC.5

#define LED_1     PORTB.3
#define LED_2     PORTB.5

#define SSI_D_PORT   PORTD  //data port
#define SSI_A_PORT   PORTC  //adres port

#define LOUDSPEAKER  PORTC.4

#define CLK_TIMER1_2 31250 //Hz - частота импульсов на входе таймера 1, деленная на 2

char SSI[11]={0xB9,0x80,0x24,0x30,0x19,0x12,0x02,0xB8,0x00,0x10,0xBF}; //цифры
char SSI_Add[4]={0,1,2,3}; //на сколько бит нужно сдвигать
char ind[4]={10,10,10,0};

char N_TIMER2=0,N2_TIMER2=0;
unsigned char key_hold=0; //bit0-plus, bit1-minus
unsigned int delta_freq=10;  

//******************************** Delay ******************************************
void delay_nop(unsigned char n_op){
  while(n_op>0){
   #asm   
     nop
   #endasm
   n_op--;
   }
}

//********************************* SPI ********************************************
// Запись 16-битного слова в AD9833
void write_to_AD9833(unsigned int WORD16){
  signed char i;

  SPI_SCK=1;
  SPI_SS=0;
  for (i=15;i>=0;i--){
   SPI_MOSI=(WORD16>>i)&0x1;
   delay_nop(200);
   SPI_SCK=0;
   delay_nop(200);
   SPI_SCK=1;
   delay_nop(200);
  }
  SPI_SS=1;
}

//******************************** AD9833 ******************************************
void init_AD9833(unsigned char Form, long int Freq){
//Form - sinus(1), triangular(2), meandr(3)
//Freq - in Hz                                
//Работа только с FREG0
  unsigned int CONTROL_RG;
  unsigned int FREG0_H; //старшие 2 байта FREG0
  unsigned int FREG0_L; //младшие 2 байта FREG0
  unsigned long int FREG_ALL;
  unsigned int npulse;
  
  if ((Form==0) || (Form==3)) { //off, mea
    // Включаем reset
    CONTROL_RG=0x0000; //регистр управления в 0
    SETBIT(CONTROL_RG,AD_B28); //FREQ_RG будем переписывать полностью - B28=1
    SETBIT(CONTROL_RG,AD_RESET); //Reset=1
    write_to_AD9833(CONTROL_RG);
    // Устанавливаем частоту 0
    write_to_AD9833(0x4000);
    write_to_AD9833(0x4000);
    // Устанавливаем фазу 3*pi/4, чтобы на выходе получить минимум синусоиды
    write_to_AD9833(0xCC00);
    //Включаем режим sleep
    SETBIT(CONTROL_RG,AD_SLEEP1); //усыпить internal clk 
    SETBIT(CONTROL_RG,AD_SLEEP12); //усыпить DAC
    // Выключаем reset
    CLIBIT(CONTROL_RG,AD_RESET); //Reset=0
    write_to_AD9833(CONTROL_RG);
    
    // Установка режима "меандр"
    if (Form==3){
      //расчет коэффициента деления
      npulse=CLK_TIMER1_2/Freq; 
      TCNT1H=0x00;TCNT1L=0x00;
      OCR1AH=(npulse>>8)&0xFF;
      OCR1AL=(npulse)&0xFF;
      TCCR1A=0x40; //переключаем PB1 на выход таймера 
      TCCR1B=0x0C; //включаем таймер
    }
  } 

  
    if ((Form==1) || (Form==2)) { //sin, tre
    CONTROL_RG=0x0000; //регистр управления в 0
    // Рассчитываем, какое число нужно записывать в FREG
    FREG_ALL=Freq*0x21; //0x21 -> (2^28)/MCLK;
    FREG0_H=((FREG_ALL>>14)&0x3FFF)|(1<<14); //(1<<14)-дописываем в биты D15, D14 адрес 01 (FREG0)
    FREG0_L=(FREG_ALL&0x3FFF)|(1<<14);       //(1<<14)-дописываем в биты D15, D14 адрес 01 (FREG0)
    
    //.................Запись в AD9833......................  
    // Включаем reset
    SETBIT(CONTROL_RG,AD_B28); //FREQ_RG будем переписывать полностью - B28=1
    SETBIT(CONTROL_RG,AD_RESET); //Reset=1
    write_to_AD9833(CONTROL_RG);
    // Устанавливаем частоту
    write_to_AD9833(FREG0_L);
    write_to_AD9833(FREG0_H);
    // Устанавливаем фазу 0 - чтобы не было мусора
    write_to_AD9833(0xC000);
    // Установка режима
    if (Form==2) SETBIT(CONTROL_RG,AD_MODE); //tre 
    // Выключаем reset
    CLIBIT(CONTROL_RG,AD_RESET); //Reset=0
    write_to_AD9833(CONTROL_RG);
  } 
}

//******************************* SET_IND **************************************
void SET_IND(unsigned int freq){
  int k;
  
  k=freq;
  ind[0]=k/1000; k=k-ind[0]*1000;
  ind[1]=k/100; k=k-ind[1]*100;   
  ind[2]=k/10; k=k-ind[2]*10;
  ind[3]=k; 
  if(ind[0]==0) ind[0]=10;
  if((ind[1]==0)&&(ind[0]==10)) ind[1]=10;
  if((ind[2]==0)&&(ind[1]==10)) ind[2]=10;  
}

//******************************** Timer2 **************************************
// Timer2 output compare interrupt service routine
interrupt [TIM2_COMPA] void timer2_compa_isr(void){
  N_TIMER2++;
  if(N_TIMER2==125){
    N_TIMER2=0;
    if (KEY_PLUS==0)  SETBIT(key_hold,0);
    if (KEY_MINUS==0) SETBIT(key_hold,1); 
    TCCR2B=0x00;
    N2_TIMER2++;
    if(N2_TIMER2==5) {
      N2_TIMER2=0;
      if(delta_freq<=100) delta_freq=delta_freq*10;
    }
  }
}

//********************************* ADC ****************************************
// Read the AD conversion result
unsigned int read_adc(unsigned char adc_input)
{
ADMUX=adc_input | (ADC_VREF_TYPE & 0xff);
// Delay needed for the stabilization of the ADC input voltage
delay_us(10);
// Start the AD conversion
ADCSRA|=0x40;
// Wait for the AD conversion to complete
while ((ADCSRA & 0x10)==0);
ADCSRA|=0x10;
return ADCW;
}

//******************************** main ****************************************
void main(void){
unsigned char i,it=0;
unsigned char key_press=0, key_rel=0; //bit0-plus, bit1-minus, bit2-mode
unsigned char mode;
unsigned int freq;
unsigned int ADC_val;
unsigned char ALERT=0;

// Crystal Oscillator division factor: 1
#pragma optsize-
CLKPR=0x80;
CLKPR=0x00;
#ifdef _OPTIMIZE_SIZE_
#pragma optsize+
#endif

// Input/Output Ports initialization
// Port B initialization
// Func7=In Func6=In Func5=Out Func4=In Func3=Out Func2=Out Func1=Out Func0=In 
// State7=T State6=T State5=0 State4=P State3=0 State2=1 State1=0 State0=P 
PORTB=0x15;
DDRB=0x2E;

// Port C initialization
// Func6=In Func5=In Func4=Out Func3=Out Func2=Out Func1=Out Func0=Out 
// State6=T State5=P State4=0 State3=0 State2=0 State1=0 State0=0 
PORTC=0x20;
DDRC=0x1F;

// Port D initialization
// Func7=Out Func6=Out Func5=Out Func4=Out Func3=Out Func2=Out Func1=Out Func0=Out 
// State7=0 State6=0 State5=0 State4=0 State3=0 State2=0 State1=0 State0=0 
PORTD=0x00;
DDRD=0xFF;

// Timer/Counter 0 initialization
// Clock source: System Clock
// Clock value: 16000,000 kHz
// Mode: CTC top=OCR0A
// OC0A output: Toggle on compare match
// OC0B output: Disconnected
TCCR0A=0x42;
TCCR0B=0x01;
TCNT0=0x00;
OCR0A=0x00;
OCR0B=0x00;

// Timer/Counter 1 initialization
// Clock source: System Clock
// Clock value: 62,500 kHz
// Mode: CTC top=OCR1A
// OC1A output: Toggle
// OC1B output: Discon.
// Noise Canceler: Off
// Input Capture on Falling Edge
// Timer1 Overflow Interrupt: Off
// Input Capture Interrupt: Off
// Compare A Match Interrupt: Off
// Compare B Match Interrupt: Off
TCCR1A=0x00;
TCCR1B=0x00;
TCNT1H=0x00;
TCNT1L=0x00;
ICR1H=0x00;
ICR1L=0x00;
OCR1AH=0x00;
OCR1AL=0x00;
OCR1BH=0x00;
OCR1BL=0x00;

// Timer/Counter 2 initialization
// Clock source: System Clock
// Clock value: 15,625 kHz
// Mode: CTC top=OCR2A
// OC2A output: Disconnected
// OC2B output: Disconnected
ASSR=0x00;
TCCR2A=0x02;
TCCR2B=0x00;
TCNT2=0x00;
OCR2A=0x7D;
OCR2B=0x00;

// External Interrupt(s) initialization
// INT0: Off
// INT1: Off
// Interrupt on any change on pins PCINT0-7: Off
// Interrupt on any change on pins PCINT8-14: Off
// Interrupt on any change on pins PCINT16-23: Off
EICRA=0x00;
EIMSK=0x00;
PCICR=0x00;

// Timer/Counter 0 Interrupt(s) initialization
TIMSK0=0x00;
// Timer/Counter 1 Interrupt(s) initialization
TIMSK1=0x00;
// Timer/Counter 2 Interrupt(s) initialization
TIMSK2=0x02;

// Analog Comparator initialization
// Analog Comparator: Off
// Analog Comparator Input Capture by Timer/Counter 1: Off
ACSR=0x80;
ADCSRB=0x00;

// ADC initialization
// ADC Clock frequency: 1000,000 kHz
// ADC Voltage Reference: AVCC pin
// ADC Auto Trigger Source: Free Running
// Digital input buffers on ADC0: On, ADC1: On, ADC2: On, ADC3: On
// ADC4: On, ADC5: On
DIDR0=0x00;
ADMUX=ADC_VREF_TYPE & 0xff;
ADCSRA=0xA4;
ADCSRB&=0xF8;

// Global enable interrupts
#asm("sei")

delay_ms(500);
//...........................начальные установки.................................
freq=0;  mode=MODE_OFF;
init_AD9833(MODE_OFF, freq);
//.........................ждем нажатия кнопки MODE..............................
while (KEY_MODE==1) {delay_ms(100);}
mode=MODE_SIN;
MEA_OUT=1; LED_1=1; LED_2=0;
//...............................................................................

while (1){
  //Цикл вывода на индикаторы
  for (i=0;i<=3;i++){
    SSI_D_PORT=(SSI_D_PORT&0x80)|SSI[ind[i]];
    //PORTC.4=(SSI[ind[i]]&0x40)>>6; //заглушка на неработающую ножку PD7
    SSI_A_PORT=(SSI_A_PORT&0xF0)|(1<<SSI_Add[i]); //изменять 0xF0
    delay_ms(5);
  }//for

  //опрос клавиатуры
  if (KEY_MODE==0) SETBIT(key_press,2); //key_press.2=1
  else {CLIBIT(key_press,2); SETBIT(key_rel,2);} //key_press.2=0; key_rel.2=1;
  
  if(((key_press&0x04)>0)&&((key_rel&0x04)>0)){ //кнопка "режим"
    CLIBIT(key_rel,2);
    ALERT=0;
    mode=(mode+1)&0x03;
    TCCR1A=0x00; //вывод порта отключить от выхода таймера
    TCCR1B=0x00; //остановить таймер 1
    switch(mode){
      case 0: MEA_OUT=1; for(i=0;i<=3;i++) {ind[i]=10;}; break;
      case 1: MEA_OUT=1; SET_IND(freq); break;
      case 2: MEA_OUT=1; SET_IND(freq); break;
      case 3: SET_IND(freq); break;
    } //switch
    init_AD9833(mode,freq);
    switch(mode){
      case 0: LED_1=0; LED_2=0; break;
      case 1: LED_1=1; LED_2=0; break;
      case 2: LED_1=0; LED_2=1; break;
      case 3: LED_1=1; LED_2=1; break;
    } //switch
  }
      
  if (ALERT==0) {
    if (KEY_PLUS==0) {SETBIT(key_press,0); TCCR2B=0x07;} //key_press.0=1
    else {CLIBIT(key_press,0); SETBIT(key_rel,0); CLIBIT(key_hold,0);} //key_press.0=0; key_rel.0=1;

    if (KEY_MINUS==0) {SETBIT(key_press,1); TCCR2B=0x07;} //key_press.1=1
    else {CLIBIT(key_press,1); SETBIT(key_rel,1); CLIBIT(key_hold,1);} //key_press.1=0; key_rel.1=1;

    if ((KEY_PLUS==1)&&(KEY_MINUS==1)) {
      TCCR2B=0x00; TCNT2=0x00;
      N_TIMER2=0; N2_TIMER2=0;
      delta_freq=10;
    }
    
    if ((((key_press&0x01)>0)&&((key_rel&0x01)>0)) || 
        (((key_press&0x01)>0)&&((key_hold&0x01)>0))) { //кнопка "плюс"
      CLIBIT(key_rel,0);
      if((key_hold&0x01)>0) {if((signed)freq<=(9999-delta_freq)) freq=freq+delta_freq;}
      else {if(freq<9999) freq++;}
      init_AD9833(mode,freq); 
      SET_IND(freq);
      switch(mode){
        case 0: LED_1=0; LED_2=0; break;
        case 1: LED_1=1; LED_2=0; break;
        case 2: LED_1=0; LED_2=1; break;
        case 3: LED_1=1; LED_2=1; break;
      } //switch       
    }

    if ((((key_press&0x02)>0)&&((key_rel&0x02)>0)) ||
        (((key_press&0x02)>0)&&((key_hold&0x02)>0))) { //кнопка "минус"
      CLIBIT(key_rel,1);
      if((key_hold&0x02)>0) {if(freq>=delta_freq) freq=freq-delta_freq;}
      else {if(freq>0) freq--;}
      init_AD9833(mode,freq); 
      SET_IND(freq);
      switch(mode){
        case 0: LED_1=0; LED_2=0; break;
        case 1: LED_1=1; LED_2=0; break;
        case 2: LED_1=0; LED_2=1; break;
        case 3: LED_1=1; LED_2=1; break;
      } //switch
    } 
   
    //Примерно раз в секунду опрашиваем вход АЦП
    it++;
    if(it==50){
      it=0;
      ADC_val=read_adc(6);
      if((ADC_val>1000) || (ADC_val<10)) {
        //ALERT - выключить всех!!!
        init_AD9833(MODE_OFF,freq); //выключаем генератор
        TCCR1B=0; MEA_OUT=0; //выключаем выход меандра
        for (i=0;i<=3;i++) ind[i]=10; //погасить индикаторы
        SSI_D_PORT=(SSI_D_PORT&0x80)|SSI[ind[3]]; //погасить 3й индикатор
        for (i=0;i<3;i++){    //пищим 3 раза
          LOUDSPEAKER=1;
          delay_ms(1000);
          LOUDSPEAKER=0; 
          delay_ms(1000);
        }//for
        LED_1=0; LED_2=0; //погасить светодиоды
        if (mode>0) mode--;
        else mode=3;
        ALERT=1; //установить флаг ALERT
      }//if(ALERT)
    }//if(it==50)
  }//if(ALERT==0)

};//while(1)
}
