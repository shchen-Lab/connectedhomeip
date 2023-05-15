/**
 * Copyright (c) 2016-2021 Bouffalolab Co., Ltd.
 *
 * Contact information:
 * web site:    https://www.bouffalolab.com/
 */

#include "demo_pwm.h"
#include "board.h"
#include <hosal_pwm.h>
#include <stdio.h>
#include <FreeRTOS.h>
#include <task.h>
#include <timers.h>
#include <semphr.h>

#define PWM_FREQ 600
#define PWM_DUTY_CYCLE 10000

#define MAX_NUM_TEST 3048
#define MIN_NUM_TEST 120

static TimerHandle_t Light_TimerHdl=NULL;
volatile static char Light_Timer_Status = 0;
static void demo_hosal_pwm_task_init(void);
static void Light_TimerHandler(TimerHandle_t p_timerhdl);
static void hard_set_color(uint8_t currLevel, uint8_t currHue, uint8_t currSat);
static uint32_t new_Rduty=0;
static uint32_t new_Gduty=0;
static uint32_t new_Bduty=0;
static uint32_t new_Cduty=0;
static uint32_t new_Wduty=0;

static uint32_t Rduty=0;
static uint32_t Gduty=0;
static uint32_t Bduty=0;
static uint32_t Cduty=0;
static uint32_t Wduty=0;


hosal_pwm_dev_t rgbcw_pwm[] = {

#if MAX_PWM_CHANNEL
    {
        .port = LED_B_PIN_PORT,
        /* pwm config */
        .config.pin        = LED_B_PIN,
        .config.duty_cycle = 0,        // duty_cycle range is 0~10000 correspond to 0~100%
        .config.freq       = PWM_FREQ, // freq range is between 0~40MHZ
    },
    {
        .port = LED_R_PIN_PORT,
        /* pwm config */
        .config.pin        = LED_R_PIN,
        .config.duty_cycle = 0,        // duty_cycle range is 0~10000 correspond to 0~100%
        .config.freq       = PWM_FREQ, // freq range is between 0~40MHZ
    },
    {
        .port = LED_G_PIN_PORT,
        /* pwm config */
        .config.pin        = LED_G_PIN,
        .config.duty_cycle = 0,        // duty_cycle range is 0~10000 correspond to 0~100%
        .config.freq       = PWM_FREQ, // freq range is between 0~40MHZ
    },
    {
        .port = LED_C_PIN_PORT,
        /* pwm config */
        .config.pin        = LED_C_PIN,
        .config.duty_cycle = 0,        // duty_cycle range is 0~10000 correspond to 0~100%
        .config.freq       = PWM_FREQ, // freq range is between 0~40MHZ
    },
    {
        .port = LED_W_PIN_PORT,
        /* pwm config */
        .config.pin        = LED_W_PIN,
        .config.duty_cycle = 0,        // duty_cycle range is 0~10000 correspond to 0~100%
        .config.freq       = PWM_FREQ, // freq range is between 0~40MHZ
    }
#else
    {
        .port = LED_PIN_PORT,
        /* pwm config */
        .config.pin        = LED_PIN,
        .config.duty_cycle = 0,        // duty_cycle range is 0~10000 correspond to 0~100%
        .config.freq       = PWM_FREQ, // freq range is between 0~40MHZ
    },
#endif
};

void demo_hosal_pwm_init(void)
{
    /* init pwm with given settings */
    for (uint32_t i = 0; i < MAX_PWM_CHANNEL; i+=2)
    {
        hosal_pwm_init(rgbcw_pwm + i);
    }
    demo_hosal_pwm_task_init();
}

void demo_hosal_pwm_start(void)
{
    /* start pwm */
    for (uint32_t i = 0; i < MAX_PWM_CHANNEL; i+=2)
    {
        hosal_pwm_start(rgbcw_pwm + i);
    }
}
static float pwmcurve_pow(float a,uint32_t b)
{
    float ret=1;
    while(b>0)
    {
        if(b&1)
            ret*=a;
        a*=a;
        b>>=1;
    }
    return ret;
}
static float demo_pwmcurve_get_duty(uint32_t p_duty)
{
    #if 0
    float curve_line=1.000768079;
    #endif
    float curve_line=0;
    if(p_duty==0)
        return curve_line;
    curve_line=10 * pwmcurve_pow(1.000768079, p_duty-1);
    return curve_line;
}

void demo_hosal_pwm_set_param(uint32_t p_Rduty,uint32_t p_Gduty,uint32_t p_Bduty,uint32_t p_Cduty,uint32_t p_Wduty)
{
    hosal_pwm_config_t para[5];
    para[0].duty_cycle = demo_pwmcurve_get_duty(p_Rduty)*100;
    para[0].freq       = PWM_FREQ;
    para[1].duty_cycle = demo_pwmcurve_get_duty(p_Gduty)*100;
    para[1].freq       = PWM_FREQ;
    para[2].duty_cycle = demo_pwmcurve_get_duty(p_Bduty)*100;
    para[2].freq       = PWM_FREQ;
    para[3].duty_cycle = demo_pwmcurve_get_duty(p_Cduty)*100;
    para[3].freq       = PWM_FREQ;
    para[4].duty_cycle = demo_pwmcurve_get_duty(p_Wduty)*100;
    para[4].freq       = PWM_FREQ;

    for (uint32_t i = 0; i < MAX_PWM_CHANNEL; i+=2)
    {
        if (para[i].duty_cycle > PWM_DUTY_CYCLE)
        {
            para[i].duty_cycle = PWM_DUTY_CYCLE;
        }
        hosal_pwm_para_chg(rgbcw_pwm + i, para[i]);
    }
}

void demo_hosal_pwm_stop(void)
{
    for (uint32_t i = 0; i < MAX_PWM_CHANNEL; i+=2)
    {
        hosal_pwm_stop(rgbcw_pwm + i);
        hosal_pwm_finalize(rgbcw_pwm + i);
    }
}

void set_level(uint8_t currLevel)
{
    printf("%s\r\n",__func__);
    #if 0
    hosal_pwm_config_t para;

    if (currLevel <= 5 && currLevel >= 1)
    {
        currLevel = 5; // avoid demo off
    }

    para.duty_cycle = currLevel * PWM_DUTY_CYCLE / 254;
    para.freq       = PWM_FREQ;

    demo_hosal_pwm_change_param(&para);
    #endif
}

void set_color_red(uint8_t currLevel)
{
    hard_set_color(currLevel, 0, 254);
}

void set_color_green(uint8_t currLevel)
{
    hard_set_color(currLevel, 84, 254);
}

void set_color_yellow(uint8_t currLevel)
{
    hard_set_color(currLevel, 42, 254);
}

static void hard_set_color(uint8_t currLevel, uint8_t currHue, uint8_t currSat)
{
#if MAX_PWM_CHANNEL
    uint16_t hue = (uint16_t) currHue * 360 / 254;
    uint8_t sat  = (uint16_t) currSat * 100 / 254;

    if (currLevel <= 5 && currLevel >= 1)
    {
        currLevel = 5; // avoid demo off
    }

    if (sat > 100)
    {
        sat = 100;
    }

    uint16_t i       = hue / 60;
    uint16_t rgb_max = currLevel;
    uint16_t rgb_min = rgb_max * (100 - sat) / 100;
    uint16_t diff    = hue % 60;
    uint16_t rgb_adj = (rgb_max - rgb_min) * diff / 60;
    uint32_t red, green, blue;

    switch (i)
    {
    case 0:
        red   = rgb_max;
        green = rgb_min + rgb_adj;
        blue  = rgb_min;
        break;
    case 1:
        red   = rgb_max - rgb_adj;
        green = rgb_max;
        blue  = rgb_min;
        break;
    case 2:
        red   = rgb_min;
        green = rgb_max;
        blue  = rgb_min + rgb_adj;
        break;
    case 3:
        red   = rgb_min;
        green = rgb_max - rgb_adj;
        blue  = rgb_max;
        break;
    case 4:
        red   = rgb_min + rgb_adj;
        green = rgb_min;
        blue  = rgb_max;
        break;
    default:
        red   = rgb_max;
        green = rgb_min;
        blue  = rgb_max - rgb_adj;
        break;
    }

    Rduty=(red*12);
    Gduty=(green*12);
    Bduty=(blue*12);
    printf("Rduty=%lx,Gduty=%lx,Bduty=%lx\r\n",Rduty,Gduty,Bduty);
    demo_hosal_pwm_set_param(Rduty,Gduty,Bduty,0,0);
#else
    set_level(currLevel);
#endif
}
void set_color(uint8_t currLevel, uint8_t currHue, uint8_t currSat)
{
    printf("%s\r\n",__func__);
    uint16_t hue = (uint16_t)currHue * 360 / 254;
    uint8_t  sat = (uint16_t)currSat * 100 / 254;

    if(sat > 100)
    {
        sat = 100;
    }

    uint16_t i       = hue / 60;
    uint16_t rgb_max = currLevel;
    uint16_t rgb_min = rgb_max * (100 - sat) / 100;
    uint16_t diff    = hue % 60;
    uint16_t rgb_adj = (rgb_max - rgb_min) * diff / 60;
    uint32_t red, green, blue;

    switch (i)
    {
    case 0:
        red   = rgb_max;
        green = rgb_min + rgb_adj;
        blue  = rgb_min;
        break;
    case 1:
        red   = rgb_max - rgb_adj;
        green = rgb_max;
        blue  = rgb_min;
        break;
    case 2:
        red   = rgb_min;
        green = rgb_max;
        blue  = rgb_min + rgb_adj;
        break;
    case 3:
        red   = rgb_min;
        green = rgb_max - rgb_adj;
        blue  = rgb_max;
        break;
    case 4:
        red   = rgb_min + rgb_adj;
        green = rgb_min;
        blue  = rgb_max;
        break;
    default:
        red   = rgb_max;
        green = rgb_min;
        blue  = rgb_max - rgb_adj;
        break;
    }

    new_Rduty=(red*12);
    new_Gduty=(green*12);
    new_Bduty=(blue*12);
    printf("now_Rduty update=%lx,now_Gduty update =%lx,now_Bduty update =%lx\r\n",new_Rduty,new_Gduty,new_Bduty);
    if(Light_TimerHdl!=NULL)
    {
        if( xTimerIsTimerActive( Light_TimerHdl ) != pdFALSE )
        {
            if(Light_TimerHdl)
                xTimerStop(Light_TimerHdl, 0); 
        }
        if( xTimerChangePeriod(Light_TimerHdl, pdMS_TO_TICKS(1), 0 ) == pdPASS )
        {
                
            Light_Timer_Status=1;
            if(Light_TimerHdl)
                xTimerStart(Light_TimerHdl, 0); 
        }

    } 

  
}
void set_temperature(uint8_t currLevel,uint16_t temperature)
{
    printf("%s\r\n",__func__);
    uint32_t hw_temp_delta=LAM_MAX_MIREDS_DEFAULT-LAM_MIN_MIREDS_DEFAULT;
    uint32_t soft_temp_delta;

    if(temperature>LAM_MAX_MIREDS_DEFAULT)
    {
        temperature=LAM_MAX_MIREDS_DEFAULT;
    }
    else if(temperature<LAM_MIN_MIREDS_DEFAULT)
    {
        temperature=LAM_MIN_MIREDS_DEFAULT;
    }
    
    soft_temp_delta=temperature-LAM_MIN_MIREDS_DEFAULT;
    soft_temp_delta*=100;

    uint32_t warm = (254*(soft_temp_delta/hw_temp_delta))/100;
    uint32_t clod  = 254-warm;
    new_Wduty=warm*12*currLevel/254;
    new_Cduty=clod*12*currLevel/254;
    printf("now_Cduty update=%lx,now_Wduty update =%lx\r\n",new_Cduty,new_Wduty);
     if(Light_TimerHdl!=NULL)
    {
        if( xTimerIsTimerActive( Light_TimerHdl ) != pdFALSE )
        {
            if(Light_TimerHdl)
                xTimerStop(Light_TimerHdl, 0); 
        }
        if( xTimerChangePeriod(Light_TimerHdl, pdMS_TO_TICKS(1), 0 ) == pdPASS )
        {
                
            Light_Timer_Status=2;
            if(Light_TimerHdl)
                xTimerStart(Light_TimerHdl, 0); 
        }

    }
}
void set_warm_temperature()
{
    hosal_pwm_config_t para[5];
    para[0].duty_cycle = 0;
    para[0].freq       = PWM_FREQ;
    para[1].duty_cycle = 0;
    para[1].freq       = PWM_FREQ;
    para[2].duty_cycle = 0;
    para[2].freq       = PWM_FREQ;
    para[3].duty_cycle = 0;
    para[3].freq       = PWM_FREQ;
    para[4].duty_cycle = PWM_DUTY_CYCLE;
    para[4].freq       = PWM_FREQ;

    for (uint32_t i = 0; i < MAX_PWM_CHANNEL; i+=2)
    {
        if (para[i].duty_cycle > PWM_DUTY_CYCLE)
        {
            para[i].duty_cycle = PWM_DUTY_CYCLE;
        }
        hosal_pwm_para_chg(rgbcw_pwm + i, para[i]);
    }
    Wduty=254*12;
    Cduty=0;
}

void set_cold_temperature(void)
{
    
    hosal_pwm_config_t para[5];
    para[0].duty_cycle = 0;
    para[0].freq       = PWM_FREQ;
    para[1].duty_cycle = 0;
    para[1].freq       = PWM_FREQ;
    para[2].duty_cycle = 0;
    para[2].freq       = PWM_FREQ;
    para[3].duty_cycle = PWM_DUTY_CYCLE;
    para[3].freq       = PWM_FREQ;
    para[4].duty_cycle = 0;
    para[4].freq       = PWM_FREQ;

    for (uint32_t i = 0; i < MAX_PWM_CHANNEL; i+=2)
    {
        if (para[i].duty_cycle > PWM_DUTY_CYCLE)
        {
            para[i].duty_cycle = PWM_DUTY_CYCLE;
        }
        hosal_pwm_para_chg(rgbcw_pwm + i, para[i]);
    }
    Wduty=0;
    Cduty=254*12;
}
void set_warm_cold_off(void)
{
       hosal_pwm_config_t para[5];
    para[0].duty_cycle = 0;
    para[0].freq       = PWM_FREQ;
    para[1].duty_cycle = 0;
    para[1].freq       = PWM_FREQ;
    para[2].duty_cycle = 0;
    para[2].freq       = PWM_FREQ;
    para[3].duty_cycle = 0;
    para[3].freq       = PWM_FREQ;
    para[4].duty_cycle = 0;
    para[4].freq       = PWM_FREQ;

    for (uint32_t i = 0; i < MAX_PWM_CHANNEL; i+=2)
    {
        if (para[i].duty_cycle > PWM_DUTY_CYCLE)
        {
            para[i].duty_cycle = PWM_DUTY_CYCLE;
        }
        hosal_pwm_para_chg(rgbcw_pwm + i, para[i]);
    }
    Wduty=0;
    Cduty=0;
}
static void Light_TimerHandler(TimerHandle_t p_timerhdl)
{

	if( Light_Timer_Status==1)
	{
		if((new_Rduty!=Rduty)||(new_Gduty!=Gduty)||(new_Bduty!=Bduty))
		{
			if(new_Rduty>Rduty)
			{
				Rduty+=2;
			}
			else if(new_Rduty<Rduty)
			{
				Rduty-=2;
			}


			if(new_Gduty>Gduty)
			{
				Gduty+=2;
			}
			else if(new_Gduty<Gduty)
			{
				Gduty-=2;
			}

			if(new_Bduty>Bduty)
			{
				Bduty+=2;
			}
			else if(new_Bduty<Bduty)
			{
				Bduty-=2;
			}
		}  
		else
		{
		    if(Light_TimerHdl)
			xTimerStop(Light_TimerHdl, 0);  
		     Light_Timer_Status=0;
		}
	}
	else if(Light_Timer_Status==2)
	{
		if((new_Cduty!=Cduty)||(new_Wduty!=Wduty))
		{
			if(new_Cduty>Cduty)
			{
				Cduty+=2;
			}
			else if(new_Cduty<Cduty)
			{
				Cduty-=2;
			}


			if(new_Wduty>Wduty)
			{
				Wduty+=2;
			}
			else if(new_Wduty<Wduty)
			{
				Wduty-=2;
			}
		}
		else
		{
		    if(Light_TimerHdl)
			xTimerStop(Light_TimerHdl, 0);  
		     Light_Timer_Status=0;
		}
            demo_hosal_pwm_set_param(0,0,0,Cduty,Wduty);
            
	}

                   
}
static void demo_hosal_pwm_task_init(void)
{

    Light_TimerHdl = xTimerCreate("zb_Light_TimerHandler", pdMS_TO_TICKS(2), pdTRUE, NULL,  Light_TimerHandler);
}