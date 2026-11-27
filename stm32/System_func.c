/* Includes ------------------------------------------------------------------*/
#include "System_func.h"

void Init_STM32F103(void){
  
  ErrorStatus HSEStartUpStatus;
  u32 remaining;
  
  RCC_DeInit();

  //Configures the different system clocks.
  /* Enable HSE */
  RCC_HSEConfig(RCC_HSE_ON);

  /* Wait till HSE is ready */
  HSEStartUpStatus = RCC_WaitForHSEStartUp();

  if(HSEStartUpStatus == SUCCESS)
  {
    /* Enable Prefetch Buffer */
    FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);

    /* Flash 2 wait state */
    FLASH_SetLatency(FLASH_Latency_2);
 	
    /* HCLK = SYSCLK */
    RCC_HCLKConfig(RCC_SYSCLK_Div1); 
  
    /* PCLK2 = HCLK */
    RCC_PCLK2Config(RCC_HCLK_Div1); 

    /* PCLK1 = HCLK/2 */
    RCC_PCLK1Config(RCC_HCLK_Div2);

    /* PLLCLK = 8MHz * 9 = 72 MHz */
    RCC_PLLConfig(RCC_PLLSource_HSE_Div1, RCC_PLLMul_9);

    /* Enable PLL */ 
    RCC_PLLCmd(ENABLE);

    /* RCC_DeInit selects HSI. Keep it if the PLL cannot start or switch.
       Peripheral rates are derived from the actual clock after this returns. */
    remaining = 65535UL;
    while (remaining && RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET) --remaining;
    if (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == SET) {
      RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
      remaining = 65535UL;
      while (remaining && RCC_GetSYSCLKSource() != 0x08) --remaining;
      if (RCC_GetSYSCLKSource() != 0x08) RCC_SYSCLKConfig(RCC_SYSCLKSource_HSI);
    }
  }

  //Configures Vector Table base location.
  #ifdef  VECT_TAB_RAM  
  /* Set the Vector Table base location at 0x20000000 */ 
  NVIC_SetVectorTable(NVIC_VectTab_RAM, 0x0); 
  #else  /* VECT_TAB_FLASH  */
  /* Set the Vector Table base location at 0x08000000 */ 
  NVIC_SetVectorTable(NVIC_VectTab_FLASH, FLASH_ADDR);   
  #endif
  
}

#ifdef  DEBUG
/*******************************************************************************
* Function Name  : assert_failed
* Description    : Reports the name of the source file and the source line number
*                  where the assert_param error has occurred.
* Input          : - file: pointer to the source file name
*                  - line: assert_param error line source number
* Output         : None
* Return         : None
*******************************************************************************/
void assert_failed(u8* file, u32 line)
{ 
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

  /* Infinite loop */
  while (1)
  {
  }
}
#endif
