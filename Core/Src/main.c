/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "sx126x.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* TODO (clock test step 6): set to 1 if step 3 shows the module has a TCXO,
 * 0 if it has a plain crystal. Use it with #if around step 4. */
#define RADIO_USE_TCXO 1
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi2;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI2_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}

static void print_radio_status(void)
{
  sx126x_chip_status_t chip_status = {0};
  sx126x_status_t radio_status = sx126x_get_status(NULL, &chip_status);
  if(radio_status != SX126X_STATUS_OK)
  {
    printf("Failed to get status: %d\n", radio_status);
  }
  else
  {
    printf("Radio chip status: %d\n", chip_status.chip_mode);
  }
  /* chip status reference:
    2 = STBY_RC   3 = STBY_XOSC   4 = FS   5 = RX   6 = TX
   */
}

/* TODO (clock test step 2): print the radio's error flags.
 *
 * - Read them with sx126x_get_device_errors(NULL, &errors).
 *   The type is sx126x_errors_mask_t (a uint16_t).
 * - Check the return value against SX126X_STATUS_OK like print_radio_status does.
 * - Print as "<label>: errors = 0x%04X".
 * - Stretch: print the name of each set bit. Names are in sx126x.h (~line 673):
 *     bit 0-4  RC64K / RC13M / PLL / ADC / IMG calibration failed
 *     bit 5    SX126X_ERRORS_XOSC_START  -> the 32 MHz clock didn't start
 *     bit 6    SX126X_ERRORS_PLL_LOCK    -> PLL didn't lock (usually follows bit 5)
 *     bit 8    SX126X_ERRORS_PA_RAMP     -> only shows up during TX
 *
 * Why: you'll call this after each step and compare before/after to see
 * what each command did.
 */
static void print_device_errors(const char *label)
{
  (void)label;
  /* TODO: implement */
}

/* TODO (clock test): check the 32 MHz reference clock and calibration.
 * Returns 0 on pass, -1 on fail.
 *
 * Background: the chip boots on a rough internal RC clock (STBY_RC, mode 2).
 * TX/RX need the precise 32 MHz reference, which is either a crystal (works
 * on its own) or a TCXO (needs power from DIO3 after SetDIO3AsTCXOCtrl).
 * Datasheet: 4.1.4 TCXO Control Block (p.25), 13.3.6 SetDIO3AsTCXOCtrl (p.87),
 * 13.1.12 Calibrate Function, 13.6.1/13.6.2 Get/ClearDeviceErrors (p.104).
 *
 * Tip: wrap each sx126x_* call in a check so you know exactly which one failed:
 *   #define RADIO_CHECK(call) do { if ((call) != SX126X_STATUS_OK) { \
 *     printf("FAIL: %s\r\n", #call); return -1; } } while (0)
 */
static int radio_clock_test(void)
{
  /* TODO step 3: find out if the module has a crystal or a TCXO.
   * - sx126x_reset(NULL)
   * - print_device_errors("after reset")
   *     (0x0020 here is normal on TCXO boards, datasheet 13.3.6)
   * - sx126x_clear_device_errors(NULL)
   * - sx126x_set_standby(NULL, SX126X_STANDBY_CFG_XOSC)
   * - print_device_errors("after XOSC") and print_radio_status()
   * Result:
   *   0x0000 + mode 3  -> crystal. Set RADIO_USE_TCXO to 0, skip step 4.
   *   0x0020           -> no clock, probably a TCXO. Do step 4.
   * Once you know the answer, you can delete this experiment.
   */

  /* TODO step 4 (only #if RADIO_USE_TCXO): power the TCXO from DIO3.
   * - sx126x_reset(NULL) first: once TCXO mode is set, only a full reset
   *   undoes it (datasheet 4.1.4). Reset between attempts.
   * - sx126x_set_standby(NULL, SX126X_STANDBY_CFG_RC)
   * - sx126x_set_dio3_as_tcxo_ctrl(NULL, SX126X_TCXO_CTRL_1_8V, delay)
   *     1.8V is what other Wio-SX1262 projects use (not confirmed by our docs).
   *     delay is in 15.625 us steps: work out the value for 5 ms.
   * - sx126x_clear_device_errors(NULL) to drop the power-on XOSC_START flag
   */

  /* TODO step 5: calibrate and verify.
   * - sx126x_cal(NULL, SX126X_CAL_ALL)
   *     The power-on calibration ran with no clock on TCXO boards, so redo it.
   *     It takes a few ms; the HAL's BUSY wait handles that.
   * - print_device_errors("after cal")              -> expect 0x0000
   * - sx126x_set_standby(NULL, SX126X_STANDBY_CFG_XOSC)
   * - print_radio_status()                          -> expect 3 (STBY_XOSC)
   * - return 0 only if errors == 0 and chip_mode == SX126X_CHIP_MODE_STBY_XOSC
   *
   * If 0x0020 remains: try delay for 10 ms, then other voltages
   * (SX126X_TCXO_CTRL_1_7V ... _3_3V), resetting between tries.
   */

  return -1;
}

/* USER CODE END 0 */



/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI2_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  /* TODO step 1: reset the radio once here instead of inside the while loop.
   * A reset wipes all radio settings, so resetting every second undoes the
   * clock setup below. */

  if (radio_clock_test() == 0)
  {
    printf("Radio clock test PASSED\r\n");
  }
  else
  {
    printf("Radio clock test FAILED\r\n");
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
    printf("Hello, World!\r\n");
    sx126x_reset(NULL); /* TODO step 1: remove, see USER CODE 2 */
    print_radio_status();
    HAL_Delay(1000);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(RADIO_RF_SW1_GPIO_Port, RADIO_RF_SW1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(RADIO_RST_GPIO_Port, RADIO_RST_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(RADIO_NSS_GPIO_Port, RADIO_NSS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LD2_Pin RADIO_RST_Pin */
  GPIO_InitStruct.Pin = LD2_Pin|RADIO_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : RADIO_RF_SW1_Pin */
  GPIO_InitStruct.Pin = RADIO_RF_SW1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(RADIO_RF_SW1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : RADIO_BUSY_Pin */
  GPIO_InitStruct.Pin = RADIO_BUSY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(RADIO_BUSY_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : RADIO_DIO1_Pin */
  GPIO_InitStruct.Pin = RADIO_DIO1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(RADIO_DIO1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : RADIO_NSS_Pin */
  GPIO_InitStruct.Pin = RADIO_NSS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(RADIO_NSS_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
