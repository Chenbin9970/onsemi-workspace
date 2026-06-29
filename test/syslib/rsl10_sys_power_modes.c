/* ----------------------------------------------------------------------------
 * Copyright (c) 2016-2017 Semiconductor Components Industries, LLC (d/b/a ON
 * Semiconductor), All Rights Reserved
 *
 * This code is the property of ON Semiconductor and may not be redistributed
 * in any form without prior written permission from ON Semiconductor.
 * The terms of use and warranty for this code are covered by contractual
 * agreements between ON Semiconductor and the licensee.
 *
 * This is Reusable Code.
 *
 * ----------------------------------------------------------------------------
 * rsl10_sys_power_modes.c
 * - Power modes support
 * ----------------------------------------------------------------------------
 * $Revision: 1.81 $
 * $Date: 2019/11/27 17:44:57 $
 * ------------------------------------------------------------------------- */

#include <rsl10.h>

/* ----------------------------------------------------------------------------
 * Sleep-mode and standby-mode related variables
 * ------------------------------------------------------------------------- */

/* RAM region to save BLE hardware registers */
uint32_t bb_registers_image[sizeof(BB_Type) / 4];

/* RAM regions to save RF front-end registers
 * Banked 8-bit registers: 0x06 - 0x08, 0x1C - 0x68, 0x6C - 0x6D, 0x9F */
static uint32_t rf_registers_image_1[0xC0 / 4];
static uint16_t rf_registers_image_2_0x06_0x07;
static uint8_t  rf_registers_image_2_0x08;
static uint32_t rf_registers_image_2_0x1C_0x6F[0x54 / 4];
static uint8_t  rf_registers_image_2_0x9F;

/* Pointer to interrupt vector table of application */
extern void *ISR_Vector_Table;

/* ----------------------------------------------------------------------------
 * Function      : void Sys_PowerModes_Sleep_Init(
 *                        struct sleep_mode_init_env_tag *sleep_mode_env)
 * ----------------------------------------------------------------------------
 * Description   : Initialize some system blocks for Sleep Mode, save RF
 *                 register and memory banks excluding 2 Mbps bank, configure
 *                 retention regulators of supply voltages
 *
 *                 Note - Since Sys_RFFE_SetTXPower() function updates the values
 *                        of a number of RF registers, call this function after
 *                        each time Sys_RFFE_SetTXPower() function is called to
 *                        ensure that updated RF register values are backed up.
 * Inputs        : sleep_mode_env    - Parameters and configurations
 *                                     for the Sleep Mode
 * Outputs       : None
 * Assumptions   : RF bank 1 (2 Mbps) does not need to be saved
 * ------------------------------------------------------------------------- */
void Sys_PowerModes_Sleep_Init(struct sleep_mode_init_env_tag *sleep_mode_env)
{
    /* Select the clock source for RTC */
    *((volatile uint8_t *) &ACS->RTC_CTRL) = sleep_mode_env->rtc_ctrl |
                                             RTC_ENABLE;

    /* Configure the low-power timer for baseband controller:
     *  - Use 32 kHz clock from RTC
     *  - Reset is released */
    ACS->BB_TIMER_CTRL = BB_CLK_PRESCALE_1 | BB_TIMER_NRESET;

    /* Setup and start DMA channel to save register bank 0
     * of RF front-end to retention memory */

    /* Clear DMA status register */
    Sys_DMA_ClearChannelStatus(sleep_mode_env->DMA_channel_RF);

    /* Setup and start the DMA */
    Sys_DMA_ChannelConfig(sleep_mode_env->DMA_channel_RF,
                          (DMA_DISABLE_INT_DISABLE  |
                           DMA_ERROR_INT_DISABLE    |
                           DMA_COMPLETE_INT_DISABLE |
                           DMA_COUNTER_INT_DISABLE  |
                           DMA_START_INT_DISABLE    |
                           DMA_DEST_WORD_SIZE_32    |
                           DMA_SRC_WORD_SIZE_32     |
                           DMA_SRC_PBUS             |
                           DMA_PRIORITY_1           |
                           DMA_TRANSFER_P_TO_M      |
                           DMA_SRC_ADDR_INC         |
                           DMA_DEST_ADDR_INC        |
                           DMA_ADDR_LIN             |
                           DMA_ENABLE),
                          sizeof(rf_registers_image_1) / 4,
                          0,
                          (uint32_t) RF_BASE,
                          (uint32_t) rf_registers_image_1);

    /* - Enable VDDMret and VDDTret regulators with custom trimming
     * - Disable VDDCret regulator */
	ACS->VDDRET_CTRL = ((((uint32_t) sleep_mode_env->VDDMRET_trim) << 
						 ACS_VDDRET_CTRL_VDDMRET_VTRIM_Pos)         |
						VDDMRET_ENABLE                              |
						(((uint32_t) sleep_mode_env->VDDTRET_trim) << 
						 ACS_VDDRET_CTRL_VDDTRET_VTRIM_Pos)         |
						VDDTRET_ENABLE                              |
						VDDCRET_DISABLE);

    /* Set wake-up configuration registers */
    ACS->WAKEUP_CFG = sleep_mode_env->wakeup_cfg;

    /* Configure WAKEUP_GP_DATA for boot from RAM
     * WAKEUP_ADDR is packed through the SYSCTRL_MEM_ACCESS_CFG register. 
     * We must be careful to set WAKEUP_ADDR_PACKED in ACS_WAKEUP_GP_DATA, as this field exists so 
     * that it can be preserved in ACS_WAKEUP_GP_DATA; if WAKEUP_ADDR_PACKED is left at 0, the 
     * device will reboot on wakeup because it is trying to restore from an invalid address. */
    SYSCTRL->WAKEUP_ADDR = sleep_mode_env->wakeup_addr;
    ACS->WAKEUP_GP_DATA = (((uint32_t) SYSCTRL_MEM_ACCESS_CFG->WAKEUP_ADDR_PACKED_BYTE) << 24) |
                          sleep_mode_env->mem_power_cfg_wakeup;

    /* Write lock keys and wake-up restore address */
    *((volatile uint32_t *) (sleep_mode_env->wakeup_addr + 0x00)) = SYSCTRL->DBG_LOCK;
    *((volatile uint32_t *) (sleep_mode_env->wakeup_addr + 0x04)) = SYSCTRL->DBG_LOCK_KEY[0];
    *((volatile uint32_t *) (sleep_mode_env->wakeup_addr + 0x08)) = SYSCTRL->DBG_LOCK_KEY[1];
    *((volatile uint32_t *) (sleep_mode_env->wakeup_addr + 0x0C)) = SYSCTRL->DBG_LOCK_KEY[2];
    *((volatile uint32_t *) (sleep_mode_env->wakeup_addr + 0x10)) = SYSCTRL->DBG_LOCK_KEY[3];
    *((volatile uint32_t *) (sleep_mode_env->wakeup_addr + 0x14)) = sleep_mode_env->app_addr;

    /* Wait until DMA is completed */
    while (!(Sys_DMA_Get_ChannelStatus(sleep_mode_env->DMA_channel_RF) &
           DMA_COMPLETE_INT_STATUS));
}

/* ----------------------------------------------------------------------------
 * Function      : void Sys_PowerModes_Sleep_Init_2Mbps(
 *                        struct sleep_mode_init_env_tag *sleep_mode_env)
 * ----------------------------------------------------------------------------
 * Description   : Initialize some system blocks for Sleep Mode, save RF
 *                 register and memory banks including 2 Mbps bank, configure
 *                 retention regulators of supply voltages
 *
 *                 Note - Since Sys_RFFE_SetTXPower() function updates the values
 *                        of a number of RF registers, call this function after
 *                        each time Sys_RFFE_SetTXPower() function is called to
 *                        ensure that updated RF register values are backed up.
 * Inputs        : sleep_mode_env    - Parameters and configurations
 *                                     for the Sleep Mode
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Sys_PowerModes_Sleep_Init_2Mbps(struct sleep_mode_init_env_tag *sleep_mode_env)
{
    uint8_t bank_select_backup;

    /* Select the clock source for RTC */
    *((volatile uint8_t *) &ACS->RTC_CTRL) = sleep_mode_env->rtc_ctrl |
                                             RTC_ENABLE;

    /* Configure the low-power timer for baseband controller:
     *  - Use 32 kHz clock from RTC
     *  - Reset is released */
    ACS->BB_TIMER_CTRL = BB_CLK_PRESCALE_1 | BB_TIMER_NRESET;

    /* Setup and start DMA channel to save register bank 0
     * of RF front-end to retention memory */

    /* Backup current bank selection and select register bank 0 */
    bank_select_backup = RF_REG05->BANK_BYTE;
    RF_REG05->BANK_BYTE = 0;

    /* Clear DMA status register */
    Sys_DMA_ClearChannelStatus(sleep_mode_env->DMA_channel_RF);

    /* Setup and start the DMA */
    Sys_DMA_ChannelConfig(sleep_mode_env->DMA_channel_RF,
                          (DMA_DISABLE_INT_DISABLE  |
                           DMA_ERROR_INT_DISABLE    |
                           DMA_COMPLETE_INT_DISABLE |
                           DMA_COUNTER_INT_DISABLE  |
                           DMA_START_INT_DISABLE    |
                           DMA_DEST_WORD_SIZE_32    |
                           DMA_SRC_WORD_SIZE_32     |
                           DMA_SRC_PBUS             |
                           DMA_PRIORITY_1           |
                           DMA_TRANSFER_P_TO_M      |
                           DMA_SRC_ADDR_INC         |
                           DMA_DEST_ADDR_INC        |
                           DMA_ADDR_LIN             |
                           DMA_ENABLE),
                          sizeof(rf_registers_image_1) / 4,
                          0,
                          (uint32_t) RF_BASE,
                          (uint32_t) rf_registers_image_1);

    /* - Enable VDDMret and VDDTret regulators with custom trimming
     * - Disable VDDCret regulator */
	ACS->VDDRET_CTRL = ((((uint32_t) sleep_mode_env->VDDMRET_trim) << 
						 ACS_VDDRET_CTRL_VDDMRET_VTRIM_Pos)         |
						VDDMRET_ENABLE                              |
						(((uint32_t) sleep_mode_env->VDDTRET_trim) << 
						 ACS_VDDRET_CTRL_VDDTRET_VTRIM_Pos)         |
						VDDTRET_ENABLE                              |
						VDDCRET_DISABLE);
    

    /* Set wake-up configuration register */
    ACS->WAKEUP_CFG = sleep_mode_env->wakeup_cfg;

    /* Configure WAKEUP_GP_DATA for boot from RAM
     * WAKEUP_ADDR is packed through the SYSCTRL_MEM_ACCESS_CFG register.
     * We must be careful to set WAKEUP_ADDR_PACKED in ACS_WAKEUP_GP_DATA, as this field exists so 
     * that it can be preserved in ACS_WAKEUP_GP_DATA; if WAKEUP_ADDR_PACKED is left at 0, the 
     * device will reboot on wakeup because it is trying to restore from an invalid address. */
    SYSCTRL->WAKEUP_ADDR = sleep_mode_env->wakeup_addr;
    ACS->WAKEUP_GP_DATA = (((uint32_t) SYSCTRL_MEM_ACCESS_CFG->WAKEUP_ADDR_PACKED_BYTE) << 24) |
                          sleep_mode_env->mem_power_cfg_wakeup;

    /* Write lock keys and wake-up restore address */
    *((volatile uint32_t *) (sleep_mode_env->wakeup_addr + 0x00)) = SYSCTRL->DBG_LOCK;
    *((volatile uint32_t *) (sleep_mode_env->wakeup_addr + 0x04)) = SYSCTRL->DBG_LOCK_KEY[0];
    *((volatile uint32_t *) (sleep_mode_env->wakeup_addr + 0x08)) = SYSCTRL->DBG_LOCK_KEY[1];
    *((volatile uint32_t *) (sleep_mode_env->wakeup_addr + 0x0C)) = SYSCTRL->DBG_LOCK_KEY[2];
    *((volatile uint32_t *) (sleep_mode_env->wakeup_addr + 0x10)) = SYSCTRL->DBG_LOCK_KEY[3];
    *((volatile uint32_t *) (sleep_mode_env->wakeup_addr + 0x14)) = sleep_mode_env->app_addr;

    /* Wait until DMA is completed */
    while (!(Sys_DMA_Get_ChannelStatus(sleep_mode_env->DMA_channel_RF) &
           DMA_COMPLETE_INT_STATUS));

    /* Select register bank 1 */
    RF_REG05->BANK_BYTE = 1;

    /* Clear DMA status register */
    Sys_DMA_ClearChannelStatus(sleep_mode_env->DMA_channel_RF);

    /* Setup and start the DMA */
    Sys_DMA_ChannelConfig(sleep_mode_env->DMA_channel_RF,
                          (DMA_DISABLE_INT_DISABLE  |
                           DMA_ERROR_INT_DISABLE    |
                           DMA_COMPLETE_INT_DISABLE |
                           DMA_COUNTER_INT_DISABLE  |
                           DMA_START_INT_DISABLE    |
                           DMA_DEST_WORD_SIZE_32    |
                           DMA_SRC_WORD_SIZE_32     |
                           DMA_SRC_PBUS             |
                           DMA_PRIORITY_1           |
                           DMA_TRANSFER_P_TO_M      |
                           DMA_SRC_ADDR_INC         |
                           DMA_DEST_ADDR_INC        |
                           DMA_ADDR_LIN             |
                           DMA_ENABLE),
                          sizeof(rf_registers_image_2_0x1C_0x6F) / 4,
                          0,
                          (uint32_t) (RF_BASE + 0x1C),
                          (uint32_t) rf_registers_image_2_0x1C_0x6F);

    /* Manually save separate words of bank 1 */
    rf_registers_image_2_0x06_0x07 = *((uint16_t *) (RF_BASE + 0x06));
    rf_registers_image_2_0x08      = *((uint8_t  *) (RF_BASE + 0x08));
    rf_registers_image_2_0x9F      = *((uint8_t  *) (RF_BASE + 0x9F));

    /* Wait until DMA is completed */
    while (!(Sys_DMA_Get_ChannelStatus(sleep_mode_env->DMA_channel_RF) &
           DMA_COMPLETE_INT_STATUS));

    /* Restore bank selection */
    RF_REG05->BANK_BYTE = bank_select_backup;
}

/* ----------------------------------------------------------------------------
 * Function      : void Sys_PowerModes_Sleep(
 *                        struct sleep_mode_env_tag *sleep_mode_env)
 * ----------------------------------------------------------------------------
 * Description   : Configure the system, save register and memory banks
 *                 of the BLE, then enter Sleep Mode
 * Inputs        : sleep_mode_env    - Parameters and configurations
 *                                     for the Sleep Mode
 * Outputs       : None
 * Assumptions   : It is safe to enter Sleep Mode (this should be checked
 *                 before calling this function), DMA channel 0 is available
 * ------------------------------------------------------------------------- */
__attribute__((weak)) void Sys_PowerModes_Sleep(struct sleep_mode_env_tag *sleep_mode_env)
{
    /* Request the baseband low power timer to go
     * into deep sleep mode (takes a few 32kHz cycles) */
    *((volatile uint16_t *)&BB->DEEPSLCNTL) = OSC_SLEEP_EN_1   |
                                              RADIO_SLEEP_EN_1 |
                                              DEEP_SLEEP_ON_1;

    /* Initialize SYSTICK counter value (32 us/step: 3 => 112 us +/- 16 us) */
    SysTick->LOAD = 3;

    /* Start the SYSTICK counter */
    SysTick->CTRL = SYSTICK_ENABLE | SYSTICK_TICKINT_ENABLE | SYSTICK_CLKSOURCE_EXTREF_CLK;

    /* Disable all unused memories */
    SYSCTRL->MEM_POWER_CFG = sleep_mode_env->mem_power_cfg;

    /* Enable boot on RAM and clear wake-up event register */
    ACS->WAKEUP_CTRL = sleep_mode_env->wakeup_ctrl;

    /* Setup and start DMA channel to save BB registers to retention memory */

    /* Setup the base addresses for the source and destination */
    DMA->SRC_BASE_ADDR[0] = (uint32_t) BB_BASE;
    DMA->DEST_BASE_ADDR[0] = (uint32_t) bb_registers_image;

    /* Setup the transfer length and transfer counter interrupt setting */
    DMA->CTRL1[0] = (sizeof(BB_Type) / 4) << DMA_CTRL1_TRANSFER_LENGTH_Pos;

    /* Configure and start the DMA channel */
    DMA->CTRL0[0] = DMA_DISABLE_INT_DISABLE  |
                    DMA_ERROR_INT_DISABLE    |
                    DMA_COMPLETE_INT_DISABLE |
                    DMA_COUNTER_INT_DISABLE  |
                    DMA_START_INT_DISABLE    |
                    DMA_DEST_WORD_SIZE_32    |
                    DMA_SRC_WORD_SIZE_32     |
                    DMA_SRC_PBUS             |
                    DMA_PRIORITY_1           |
                    DMA_TRANSFER_P_TO_M      |
                    DMA_SRC_ADDR_INC         |
                    DMA_DEST_ADDR_INC        |
                    DMA_ADDR_LIN             |
                    DMA_ENABLE;

    /* Wait for SYSTICK interrupt
     * (to avoid continuously polling the DMA_CTRL0 and BBIF_STATUS) */
    SYS_WAIT_FOR_INTERRUPT;

    /* Acknowledge SYSTICK interrupt manually */
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

    /* Disable the SYSTICK interrupt and timer */
    SysTick->CTRL = SYSTICK_DISABLE;

    /* Wait until DMA is completed */
    while (DMA_CTRL0->ENABLE_ALIAS);

    /* Wait until the baseband has switched to the low power clock */
    while (BBIF_STATUS->CLK_STATUS_ALIAS == MASTER_CLK_BITBAND);

    /* Switch the system clock to RC OSC */
    CLK_SYS_CFG->SYSCLK_SRC_SEL_BYTE = SYSCLK_CLKSRC_RCCLK_BYTE;

    /* Disable the RF front-end (access is automatically removed) */
    SYSCTRL->RF_POWER_CFG = RF_POWER_DISABLE;

    /* Lower DC-DC max current to 16 mA minimize in-rush current */
    ACS_VCC_CTRL->ICH_TRIM_BYTE = VCC_ICHTRIM_16MA_BYTE;

    /* Ensure that the DEEPSLCNTL baseband register (0x30) is reset
     * (to prevent the baseband going back to sleep at wake-up) */
    *((uint8_t *)&bb_registers_image[0x30 / 4]) = OSC_SLEEP_EN_0 | RADIO_SLEEP_EN_0 | DEEP_SLEEP_ON_0;

    /* Enter Sleep Mode (becomes effective after WFI instruction) */
    ACS->PWR_MODES_CTRL = PWR_SLEEP_MODE;

    /* Wait until the baseband low power timer is in deep sleep mode
     * and properly isolated */
    while (BBIF_STATUS->OSC_EN_ALIAS == OSC_ENABLED_BITBAND);

    /* Wait for interrupt */
    SYS_WAIT_FOR_INTERRUPT;
}

/* ----------------------------------------------------------------------------
 * Function      : void Sys_PowerModes_Wakeup(void)
 * ----------------------------------------------------------------------------
 * Description   : Execute steps required to wake up the system from Sleep Mode
 *                 RF register bank 1 (2 Mbps) is not restored
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : - DMA channels 0 and 1 are available
 *                 - Start RC oscillator is calibrated to 3 MHz
 *                 - RF bank 1 (2 Mbps) does not need to be restored
 * ------------------------------------------------------------------------- */
void Sys_PowerModes_Wakeup(void)
{
    /* Initialize SYSTICK counter value (32 us/step : 24 => 784 us +/- 16 us)
     * Optimized for RC running trimmed at 3 MHz and 1.1 ms BLE wake-up time */
    SysTick->LOAD = 24;

    /* Start the SYSTICK counter */
    SysTick->CTRL = SYSTICK_ENABLE | SYSTICK_TICKINT_ENABLE | SYSTICK_CLKSOURCE_EXTREF_CLK;

    /* Wait until VDDRF supply has powered up */
    while (ACS_VDDRF_CTRL->READY_BYTE == VDDRF_NOT_READY_BYTE);

    /* Enable RF power switches. It take a few cycles before the RF access can
     * be enabled, therefore we do some other stuff in the meantime */
    SYSCTRL->RF_POWER_CFG = RF_POWER_ENABLE;

    /* Update vector table */
    SCB->VTOR = (unsigned int) (&ISR_Vector_Table);

    /* Mask all interrupts */
    __disable_irq();

    /* Remove RF isolation: no need to enable the RF IRQ (only used in custom mode) */
    SYSCTRL->RF_ACCESS_CFG = RF_ACCESS_ENABLE | RF_IRQ_ACCESS_DISABLE;

    /* Restore XTAL trimming to speed up starting time */
    RF->PLL_CTRL = rf_registers_image_1[0xA4 / 4];

    /* Restore XTAL parameters and start the 48 MHz oscillator */
    RF->XTAL_CTRL = rf_registers_image_1[0xAC / 4];

    /* Setup and start DMA channel to restore BB registers from retention memory */

    /* Setup the base addresses for the source and destination */
    DMA->SRC_BASE_ADDR[0] = (uint32_t) bb_registers_image;
    DMA->DEST_BASE_ADDR[0] = (uint32_t) BB_BASE;

    /* Setup the transfer length and transfer counter interrupt setting */
    DMA->CTRL1[0] = (sizeof(BB_Type) / 4) << DMA_CTRL1_TRANSFER_LENGTH_Pos;

    /* Configure the DMA channel */
    DMA->CTRL0[0] = DMA_DISABLE_INT_DISABLE  |
                    DMA_ERROR_INT_DISABLE    |
                    DMA_COMPLETE_INT_DISABLE |
                    DMA_COUNTER_INT_DISABLE  |
                    DMA_START_INT_DISABLE    |
                    DMA_DEST_WORD_SIZE_32    |
                    DMA_SRC_WORD_SIZE_32     |
                    DMA_DEST_PBUS            |
                    DMA_PRIORITY_1           |
                    DMA_TRANSFER_M_TO_P      |
                    DMA_SRC_ADDR_INC         |
                    DMA_DEST_ADDR_INC        |
                    DMA_ADDR_LIN             |
                    DMA_ENABLE;

    /* Setup and start DMA channel to restore registers bank 0
     * in RF front-end from retention memory */

    /* Setup the base addresses for the source and destination */
    DMA->SRC_BASE_ADDR[1] = (uint32_t) rf_registers_image_1;
    DMA->DEST_BASE_ADDR[1] = (uint32_t) RF_BASE;

    /* Setup the transfer length and transfer counter interrupt setting
     * Note that the last register (RF_REG2F) is not restored later to gate
     * the RF clock until it is really needed */
    DMA->CTRL1[1] = ((sizeof(rf_registers_image_1) / 4) - 1) << DMA_CTRL1_TRANSFER_LENGTH_Pos;

    /* Configure the DMA channel */
    DMA->CTRL0[1] = DMA_DISABLE_INT_DISABLE  |
                    DMA_ERROR_INT_DISABLE    |
                    DMA_COMPLETE_INT_DISABLE |
                    DMA_COUNTER_INT_DISABLE  |
                    DMA_START_INT_DISABLE    |
                    DMA_DEST_WORD_SIZE_32    |
                    DMA_SRC_WORD_SIZE_32     |
                    DMA_DEST_PBUS            |
                    DMA_PRIORITY_1           |
                    DMA_TRANSFER_M_TO_P      |
                    DMA_SRC_ADDR_INC         |
                    DMA_DEST_ADDR_INC        |
                    DMA_ADDR_LIN             |
                    DMA_ENABLE;

    /* Enable BLE interrupts */
    NVIC->ISER[1] = NVIC_BLE_EVENT_INT_ENABLE      |
                    NVIC_BLE_RX_INT_ENABLE         |
                    NVIC_BLE_CRYPT_INT_ENABLE      |
                    NVIC_BLE_ERROR_INT_ENABLE      |
                    NVIC_BLE_SW_INT_ENABLE         |
                    NVIC_BLE_GROSSTGTIM_INT_ENABLE |
                    NVIC_BLE_FINETGTIM_INT_ENABLE  |
                    NVIC_BLE_CSCNT_INT_ENABLE      |
                    NVIC_BLE_SLP_INT_ENABLE;

    /* Wait for SYSTICK interrupt (to power up flash as late as possible) */
    SYS_WAIT_FOR_INTERRUPT;

    /* Acknowledge SYSTICK interrupt manually */
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

    /* Initialize SYSTICK counter value (32 us/step : 1 => 64 us) */
    SysTick->LOAD = 1;

    /* Start the SYSTICK counter */
    SysTick->CTRL = SYSTICK_ENABLE | SYSTICK_TICKINT_ENABLE | SYSTICK_CLKSOURCE_EXTREF_CLK;

    /* Restore default DC-DC max current for normal operation */
    ACS_VCC_CTRL->ICH_TRIM_BYTE = VCC_ICHTRIM_80MA_BYTE;

    /* Power up the Flash */
    SYSCTRL_MEM_POWER_CFG->FLASH_POWER_ALIAS = FLASH_POWER_ENABLE_BITBAND;
    SYSCTRL_MEM_ACCESS_CFG->FLASH_ACCESS_ALIAS = FLASH_ACCESS_ENABLE_BITBAND;

    /* Wait for SYSTICK interrupt (to wait for the flash to be powered up) */
    SYS_WAIT_FOR_INTERRUPT;

    /* Put the flash in low power mode */
    FLASH->CMD_CTRL = CMD_SET_LOW_POWER;

    /* Acknowledge SYSTICK interrupt manually */
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

    /* Disable the SYSTICK interrupt and timer */
    SysTick->CTRL = SYSTICK_DISABLE;

    /* Stop masking interrupts */
    __enable_irq();

    /* Enable the baseband */
    BBIF->CTRL = BB_CLK_ENABLE | BBCLK_DIVIDER_8 | BB_DEEP_SLEEP;

    /* Check/wait until 48 MHz oscillator is started */
    while (RF_REG39->ANALOG_INFO_CLK_DIG_READY_ALIAS !=
           ANALOG_INFO_CLK_DIG_READY_BITBAND);

    /* Enable 48 MHz oscillator divider at desired prescale value */
    RF->REG2F = rf_registers_image_1[0xBC / 4];

    /* Note: There is no need to check that DMA0 and DMA1 have completed
     * They will have completed already after a few SYSTICK cycles */
}

/* ----------------------------------------------------------------------------
 * Function      : void Sys_PowerModes_Wakeup_2Mbps(void)
 * ----------------------------------------------------------------------------
 * Description   : Execute steps required to wake up the system from Sleep Mode
 *                 RF register bank 1 (2 Mbps) is also restored
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : - DMA channels 0 and 1 are available
 *                 - Start RC oscillator is calibrated to 3 MHz
 * ------------------------------------------------------------------------- */
void Sys_PowerModes_Wakeup_2Mbps(void)
{
    /* Initialize SYSTICK counter value (32 us/step : 14 => 464 us +/- 16 us)
     * Optimized for RC running trimmed at 3 MHz and 1.1 ms BLE wake-up time */
    SysTick->LOAD = 14;

    /* Start the SYSTICK counter */
    SysTick->CTRL = SYSTICK_ENABLE | SYSTICK_TICKINT_ENABLE | SYSTICK_CLKSOURCE_EXTREF_CLK;

    /* Wait until VDDRF supply has powered up */
    while (ACS_VDDRF_CTRL->READY_BYTE == VDDRF_NOT_READY_BYTE);

    /* Enable RF power switches. It take a few cycles before the RF access can
     * be enabled, therefore we do some other stuff in the meantime */
    SYSCTRL->RF_POWER_CFG = RF_POWER_ENABLE;

    /* Update vector table */
    SCB->VTOR = (unsigned int) (&ISR_Vector_Table);

    /* Mask all interrupts */
    __disable_irq();

    /* Remove RF isolation: no need to enable the RF IRQ (only used in custom mode) */
    SYSCTRL->RF_ACCESS_CFG = RF_ACCESS_ENABLE | RF_IRQ_ACCESS_DISABLE;

    /* Restore XTAL trimming to speed up starting time */
    RF->PLL_CTRL = rf_registers_image_1[0xA4 / 4];

    /* Restore XTAL parameters and start the 48 MHz oscillator */
    RF->XTAL_CTRL = rf_registers_image_1[0xAC / 4];

    /* Setup and start DMA channel to restore BB registers from retention memory */

    /* Setup the base addresses for the source and destination */
    DMA->SRC_BASE_ADDR[0] = (uint32_t) bb_registers_image;
    DMA->DEST_BASE_ADDR[0] = (uint32_t) BB_BASE;

    /* Setup the transfer length and transfer counter interrupt setting */
    DMA->CTRL1[0] = (sizeof(BB_Type) / 4) << DMA_CTRL1_TRANSFER_LENGTH_Pos;

    /* Configure the DMA channel */
    DMA->CTRL0[0] = DMA_DISABLE_INT_DISABLE  |
                    DMA_ERROR_INT_DISABLE    |
                    DMA_COMPLETE_INT_DISABLE |
                    DMA_COUNTER_INT_DISABLE  |
                    DMA_START_INT_DISABLE    |
                    DMA_DEST_WORD_SIZE_32    |
                    DMA_SRC_WORD_SIZE_32     |
                    DMA_DEST_PBUS            |
                    DMA_PRIORITY_1           |
                    DMA_TRANSFER_M_TO_P      |
                    DMA_SRC_ADDR_INC         |
                    DMA_DEST_ADDR_INC        |
                    DMA_ADDR_LIN             |
                    DMA_ENABLE;

    /* Setup and start DMA channel to restore registers bank 0
     * in RF front-end from retention memory */

    /* Setup the base addresses for the source and destination */
    DMA->SRC_BASE_ADDR[1] = (uint32_t) rf_registers_image_1;
    DMA->DEST_BASE_ADDR[1] = (uint32_t) RF_BASE;

    /* Setup the transfer length and transfer counter interrupt setting
     * Note that the last register (RF_REG2F) is not restored later to gate
     * the RF clock until it is really needed */
    DMA->CTRL1[1] = ((sizeof(rf_registers_image_1) / 4) - 1) << DMA_CTRL1_TRANSFER_LENGTH_Pos;

    /* Configure the DMA channel */
    DMA->CTRL0[1] = DMA_DISABLE_INT_DISABLE  |
                    DMA_ERROR_INT_DISABLE    |
                    DMA_COMPLETE_INT_DISABLE |
                    DMA_COUNTER_INT_DISABLE  |
                    DMA_START_INT_DISABLE    |
                    DMA_DEST_WORD_SIZE_32    |
                    DMA_SRC_WORD_SIZE_32     |
                    DMA_DEST_PBUS            |
                    DMA_PRIORITY_1           |
                    DMA_TRANSFER_M_TO_P      |
                    DMA_SRC_ADDR_INC         |
                    DMA_DEST_ADDR_INC        |
                    DMA_ADDR_LIN             |
                    DMA_ENABLE;

    /* Enable BLE interrupts */
    NVIC->ISER[1] = NVIC_BLE_EVENT_INT_ENABLE      |
                    NVIC_BLE_RX_INT_ENABLE         |
                    NVIC_BLE_CRYPT_INT_ENABLE      |
                    NVIC_BLE_ERROR_INT_ENABLE      |
                    NVIC_BLE_SW_INT_ENABLE         |
                    NVIC_BLE_GROSSTGTIM_INT_ENABLE |
                    NVIC_BLE_FINETGTIM_INT_ENABLE  |
                    NVIC_BLE_CSCNT_INT_ENABLE      |
                    NVIC_BLE_SLP_INT_ENABLE;

    /* Wait for SYSTICK interrupt (to wait for the DMA to have completed) */
    SYS_WAIT_FOR_INTERRUPT;

    /* Acknowledge SYSTICK interrupt manually */
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

    /* Initialize SYSTICK counter value (32 us/step : 9 => 320 us) */
    SysTick->LOAD = 9;

    /* Start the SYSTICK counter */
    SysTick->CTRL = SYSTICK_ENABLE | SYSTICK_TICKINT_ENABLE | SYSTICK_CLKSOURCE_EXTREF_CLK;

    /* Select registers bank 1 */
    RF_REG05->BANK_BYTE = 1;

    /* Manually restore separate words of bank 1 */
    *((uint16_t *) (RF_BASE + 0x06)) = rf_registers_image_2_0x06_0x07;
    *((uint8_t  *) (RF_BASE + 0x08)) = rf_registers_image_2_0x08;
    *((uint8_t  *) (RF_BASE + 0x9F)) = rf_registers_image_2_0x9F;

    /* Setup and start DMA channel to restore registers bank 1
     * in RF front-end from retention memory */

    /* Setup the base addresses for the source and destination */
    DMA->SRC_BASE_ADDR[1] = (uint32_t) rf_registers_image_2_0x1C_0x6F;
    DMA->DEST_BASE_ADDR[1] = (uint32_t) (RF_BASE + 0x1C);

    /* Setup the transfer length and transfer counter interrupt setting */
    DMA->CTRL1[1] = (sizeof(rf_registers_image_2_0x1C_0x6F) / 4) << DMA_CTRL1_TRANSFER_LENGTH_Pos;

    /* Configure the DMA channel */
    DMA->CTRL0[1] = DMA_DISABLE_INT_DISABLE  |
                    DMA_ERROR_INT_DISABLE    |
                    DMA_COMPLETE_INT_DISABLE |
                    DMA_COUNTER_INT_DISABLE  |
                    DMA_START_INT_DISABLE    |
                    DMA_DEST_WORD_SIZE_32    |
                    DMA_SRC_WORD_SIZE_32     |
                    DMA_DEST_PBUS            |
                    DMA_PRIORITY_1           |
                    DMA_TRANSFER_M_TO_P      |
                    DMA_SRC_ADDR_INC         |
                    DMA_DEST_ADDR_INC        |
                    DMA_ADDR_LIN             |
                    DMA_ENABLE;

    /* Enable BLE interrupts */
    NVIC->ISER[1] = NVIC_BLE_EVENT_INT_ENABLE      |
                    NVIC_BLE_RX_INT_ENABLE         |
                    NVIC_BLE_CRYPT_INT_ENABLE      |
                    NVIC_BLE_ERROR_INT_ENABLE      |
                    NVIC_BLE_SW_INT_ENABLE         |
                    NVIC_BLE_GROSSTGTIM_INT_ENABLE |
                    NVIC_BLE_FINETGTIM_INT_ENABLE  |
                    NVIC_BLE_CSCNT_INT_ENABLE      |
                    NVIC_BLE_SLP_INT_ENABLE;

    /* Wait for SYSTICK interrupt (to power up flash as late as possible) */
    SYS_WAIT_FOR_INTERRUPT;

    /* Acknowledge SYSTICK interrupt manually */
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

    /* Initialize SYSTICK counter value (32 us/step : 1 => 64 us) */
    SysTick->LOAD = 1;

    /* Start the SYSTICK counter */
    SysTick->CTRL = SYSTICK_ENABLE | SYSTICK_TICKINT_ENABLE | SYSTICK_CLKSOURCE_EXTREF_CLK;

    /* Restore default DC-DC max current for normal operation */
    ACS_VCC_CTRL->ICH_TRIM_BYTE = VCC_ICHTRIM_80MA_BYTE;

    /* Power up the Flash */
    SYSCTRL_MEM_POWER_CFG->FLASH_POWER_ALIAS = FLASH_POWER_ENABLE_BITBAND;
    SYSCTRL_MEM_ACCESS_CFG->FLASH_ACCESS_ALIAS = FLASH_ACCESS_ENABLE_BITBAND;

    /* Wait for SYSTICK interrupt (to wait for the flash to be powered up) */
    SYS_WAIT_FOR_INTERRUPT;

    /* Put the flash in low power mode */
    FLASH->CMD_CTRL = CMD_SET_LOW_POWER;

    /* Acknowledge SYSTICK interrupt manually */
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

    /* Disable the SYSTICK interrupt and timer */
    SysTick->CTRL = SYSTICK_DISABLE;

    /* Stop masking interrupts */
    __enable_irq();

    /* Select registers bank 0 */
    RF_REG05->BANK_BYTE = 0;

    /* Enable the baseband */
    BBIF->CTRL = BB_CLK_ENABLE | BBCLK_DIVIDER_8 | BB_DEEP_SLEEP;

    /* Check/wait until 48 MHz oscillator is started */
    while (RF_REG39->ANALOG_INFO_CLK_DIG_READY_ALIAS !=
           ANALOG_INFO_CLK_DIG_READY_BITBAND);

    /* Enable 48 MHz oscillator divider at desired prescale value */
    RF->REG2F = rf_registers_image_1[0xBC / 4];

    /* Note: There is no need to check that DMA0 and DMA1 have completed
     * They will have completed already after a few SYSTICK cycles */
}

/* ----------------------------------------------------------------------------
 * Function      : void Sys_PowerModes_Sleep_WakeupFromFlash(
 *                           struct sleep_mode_flash_env_tag *sleep_mode_env)
 * ----------------------------------------------------------------------------
 * Description   : Configure the system and enter Sleep Mode
 *                 (wake up from flash)
 * Inputs        : sleep_mode_env    - Parameters and configurations
 *                                     for the Sleep Mode
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Sys_PowerModes_Sleep_WakeupFromFlash(
                       struct sleep_mode_flash_env_tag *sleep_mode_env)
{
    /* Enable retention regulators with custom trimming */
    ACS->VDDRET_CTRL = (((uint32_t) sleep_mode_env->VDDMRET_trim)   << 
	                    ACS_VDDRET_CTRL_VDDMRET_VTRIM_Pos)                                    |
                       (((uint32_t) sleep_mode_env->VDDMRET_enable) <<
					    ACS_VDDRET_CTRL_VDDMRET_ENABLE_Pos)                                   |
                       (((uint32_t) sleep_mode_env->VDDTRET_trim)   << 
					    ACS_VDDRET_CTRL_VDDTRET_VTRIM_Pos)                                    |
                       (((uint32_t) sleep_mode_env->VDDTRET_enable) << 
					    ACS_VDDRET_CTRL_VDDTRET_ENABLE_Pos)                                   |
                       (((uint32_t) sleep_mode_env->VDDCRET_trim)   << 
					    ACS_VDDRET_CTRL_VDDCRET_VTRIM_Pos)                                    |
                       (((uint32_t) sleep_mode_env->VDDCRET_enable) << 
					    ACS_VDDRET_CTRL_VDDCRET_ENABLE_Pos);

    /* Set wake-up configuration registers */
    ACS->WAKEUP_CFG = sleep_mode_env->wakeup_cfg;

    /* Set wake-up control/status registers */
    ACS->WAKEUP_CTRL = sleep_mode_env->wakeup_ctrl;

    /* Power-off/retain memory instances */
    SYSCTRL->MEM_POWER_CFG = sleep_mode_env->mem_power_cfg |
                             PROM_POWER_ENABLE             |
                             FLASH_POWER_ENABLE;

    /* Switch SYSCLK to RC startup oscillator */
    CLK_SYS_CFG->SYSCLK_SRC_SEL_BYTE = SYSCLK_CLKSRC_RCCLK_BYTE;

    /* Enter Sleep Mode */
    ACS->PWR_MODES_CTRL= PWR_SLEEP_MODE;

    /* Wait for interrupt */
    SYS_WAIT_FOR_INTERRUPT;
}

/* ----------------------------------------------------------------------------
 * IFunction     : void Sys_PowerModes_Standby_Init(
 *                        struct standby_mode_env_tag *standby_mode_env)
 * ----------------------------------------------------------------------------
 * Description   : Initialize some system blocks for Standby Mode, save RF
 *                 registers and memory banks
 *
 *                 Note - Since Sys_RFFE_SetTXPower() function updates the values
 *                        of a number of RF registers, call this function after
 *                        each time Sys_RFFE_SetTXPower() function is called to
 *                        ensure that updated RF register values are backed up.
 * Inputs        : standby_mode_env    - Parameters and configurations
 *                                       for the Standby Mode
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Sys_PowerModes_Standby_Init(struct standby_mode_env_tag *standby_mode_env)
{

    uint8_t bank_select_backup;

    /* Select the clock source for RTC */
    *((volatile uint8_t *) &ACS->RTC_CTRL) = standby_mode_env->RTC_clk_src |
                                             RTC_ENABLE;

    /* Configure the low-power timer for baseband controller:
     *  - Use 32 kHz clock from RTC
     *  - Reset is released */
    ACS->BB_TIMER_CTRL = BB_CLK_PRESCALE_1 | BB_TIMER_NRESET;

    /* Setup and start DMA channel to save registers bank 0
     * of RF front-end to retention memory */

    /* Backup current bank selection and select register bank 0 */
    bank_select_backup = RF_REG05->BANK_BYTE;
    RF_REG05->BANK_BYTE = 0;

    /* Clear DMA status register */
    Sys_DMA_ClearChannelStatus(standby_mode_env->DMA_channel_RF);

    /* Setup and start the DMA */
    Sys_DMA_ChannelConfig(standby_mode_env->DMA_channel_RF,
                          (DMA_DISABLE_INT_DISABLE  |
                           DMA_ERROR_INT_DISABLE    |
                           DMA_COMPLETE_INT_DISABLE |
                           DMA_COUNTER_INT_DISABLE  |
                           DMA_START_INT_DISABLE    |
                           DMA_DEST_WORD_SIZE_32    |
                           DMA_SRC_WORD_SIZE_32     |
                           DMA_SRC_PBUS             |
                           DMA_PRIORITY_1           |
                           DMA_TRANSFER_P_TO_M      |
                           DMA_SRC_ADDR_INC         |
                           DMA_DEST_ADDR_INC        |
                           DMA_ADDR_LIN             |
                           DMA_ENABLE),
                          sizeof(rf_registers_image_1) / 4,
                          0,
                          (uint32_t) RF_BASE,
                          (uint32_t) rf_registers_image_1);

    /* Wait until DMA is completed */
    while (!(Sys_DMA_Get_ChannelStatus(standby_mode_env->DMA_channel_RF) &
           DMA_COMPLETE_INT_STATUS));

    /* Select register bank 1 */
    RF_REG05->BANK_BYTE = 1;

    /* Clear DMA status register */
    Sys_DMA_ClearChannelStatus(standby_mode_env->DMA_channel_RF);

    /* Setup and start the DMA */
    Sys_DMA_ChannelConfig(standby_mode_env->DMA_channel_RF,
                          (DMA_DISABLE_INT_DISABLE  |
                           DMA_ERROR_INT_DISABLE    |
                           DMA_COMPLETE_INT_DISABLE |
                           DMA_COUNTER_INT_DISABLE  |
                           DMA_START_INT_DISABLE    |
                           DMA_DEST_WORD_SIZE_32    |
                           DMA_SRC_WORD_SIZE_32     |
                           DMA_SRC_PBUS             |
                           DMA_PRIORITY_1           |
                           DMA_TRANSFER_P_TO_M      |
                           DMA_SRC_ADDR_INC         |
                           DMA_DEST_ADDR_INC        |
                           DMA_ADDR_LIN             |
                           DMA_ENABLE),
                          sizeof(rf_registers_image_2_0x1C_0x6F) / 4,
                          0,
                          (uint32_t) (RF_BASE + 0x1C),
                          (uint32_t) rf_registers_image_2_0x1C_0x6F);

    /* Manually save separate words of bank 1 */
    rf_registers_image_2_0x06_0x07 = *((uint16_t *) (RF_BASE + 0x06));
    rf_registers_image_2_0x08      = *((uint8_t  *) (RF_BASE + 0x08));
    rf_registers_image_2_0x9F      = *((uint8_t  *) (RF_BASE + 0x9F));

    /* Wait until DMA is completed */
    while (!(Sys_DMA_Get_ChannelStatus(standby_mode_env->DMA_channel_RF) &
           DMA_COMPLETE_INT_STATUS));
    /* Restore bank selection */
    RF_REG05->BANK_BYTE = bank_select_backup;

    /* Set wake-up configuration registers */
    ACS->WAKEUP_CFG = standby_mode_env->wakeup_cfg;
}

/* ----------------------------------------------------------------------------
 * Function      : void Sys_PowerModes_Standby(
 *                     struct standby_mode_env_tag *standby_mode_env)
 * ----------------------------------------------------------------------------
 * Description   : Configure the system and enter Standby Mode
 * Inputs        : - standby_mode_env    - Parameters and configurations
 *                                         for the Standby Mode
 * Outputs       : None
 * Assumptions   : - Any retention regulator needed has been enabled
 *                 - Desired wake-up source has been set up before calling this
 *                   function
 *                 - At least one interrupt needs to be enabled before going
 *                   to Standby Mode and asserted after wake-up event to
 *                   wake up the ARM Cortex-M3 processor from WFI
 * ------------------------------------------------------------------------- */
void Sys_PowerModes_Standby(struct standby_mode_env_tag *standby_mode_env)
{
    /* Enable wake-up interrupt */
    NVIC_ClearPendingIRQ(WAKEUP_IRQn);
    NVIC_EnableIRQ(WAKEUP_IRQn);

    /* Request the baseband to go into deep sleep mode
     * (takes a few 32kHz cycles) */
    *((volatile uint16_t *)&BB->DEEPSLCNTL) = OSC_SLEEP_EN_1   |
                                              RADIO_SLEEP_EN_1 |
                                              DEEP_SLEEP_ON_1;

    /* Wait until the baseband has switched to the low power clock */
    while (BBIF_STATUS->CLK_STATUS_ALIAS == MASTER_CLK_BITBAND);

    /* Set wake-up control/status registers */
    ACS->WAKEUP_CTRL = standby_mode_env->wakeup_ctrl;

    /* Switch SYSCLK to RC OSC */
    CLK_SYS_CFG->SYSCLK_SRC_SEL_BYTE = SYSCLK_CLKSRC_RCCLK_BYTE;

    /* Stop XTAL48MHz oscillator */
    /* Stop the 48 MHz oscillator without changing the other register bits */
    RF->XTAL_CTRL = (RF->XTAL_CTRL | XTAL_CTRL_DISABLE_OSCILLATOR);

    /* Turn off RF */
    /* Put RF into isolation */
    SYSCTRL_RF_ACCESS_CFG->RF_ACCESS_ALIAS = RF_ACCESS_DISABLE_BITBAND;

    /* Disable RF power switches */
    SYSCTRL_RF_POWER_CFG->RF_POWER_ALIAS = RF_POWER_DISABLE_BITBAND;

    /* Disable RF power amplifier power supply */
    ACS_VDDPA_CTRL->VDDPA_SW_CTRL_ALIAS = VDDPA_SW_VDDRF_BITBAND;
    ACS_VDDPA_CTRL->ENABLE_ALIAS = VDDPA_DISABLE_BITBAND;

    /* Disable VDDRF power supply */
    ACS_VDDRF_CTRL->ENABLE_ALIAS = VDDRF_DISABLE_BITBAND;

    /* Enter Standby Mode (becomes effective after WFI instruction) */
    ACS->PWR_MODES_CTRL= PWR_STANDBY_MODE;

    /* Wait until the baseband low power timer is in deep sleep mode
     * and properly isolated */
    while (BBIF_STATUS->OSC_EN_ALIAS == OSC_ENABLED_BITBAND);

    /* Wait for interrupt */
    SYS_WAIT_FOR_INTERRUPT;
}

/* ----------------------------------------------------------------------------
 * Function      : void Sys_PowerModes_Standby_Wakeup(
 *                    struct standby_mode_env_tag *standby_mode_env)
 * ----------------------------------------------------------------------------
 * Description   : Execute steps required to wake up the system from
 *                 Standby Mode
 * Inputs        : Pre-defined parameters and configurations
 *                 for the Standby Mode
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Sys_PowerModes_Standby_Wakeup(struct standby_mode_env_tag *standby_mode_env)
{
    /* Enable VDDRF supply without changing trimming settings */
    ACS_VDDRF_CTRL->ENABLE_ALIAS = VDDRF_ENABLE_BITBAND;
    ACS_VDDRF_CTRL->CLAMP_ALIAS = VDDRF_DISABLE_HIZ_BITBAND;

    /* Wait until VDDRF supply has powered up */
    while (ACS_VDDRF_CTRL->READY_BYTE == VDDRF_NOT_READY_BYTE);

    /* Enable RF power switches */
    SYSCTRL_RF_POWER_CFG->RF_POWER_ALIAS = RF_POWER_ENABLE_BITBAND;

    /* Remove RF isolation */
    SYSCTRL_RF_ACCESS_CFG->RF_ACCESS_ALIAS = RF_ACCESS_ENABLE_BITBAND;

    /* Mask all interrupts */
    __disable_irq();

    /* Start the 48 MHz oscillator without changing the other register bits */
    RF->XTAL_CTRL = ((RF->XTAL_CTRL & ~XTAL_CTRL_DISABLE_OSCILLATOR) |
                     XTAL_CTRL_REG_VALUE_SEL_INTERNAL);

    /* Enable 48 MHz oscillator divider at desired prescale value */
    RF_REG2F->CK_DIV_1_6_CK_DIV_1_6_BYTE = CK_DIV_1_6_PRESCALE_6_BYTE;

    /* Setup and start DMA channel to restore registers bank 0
     * in RF front-end from retention memory */

    /* Select registers bank 0 */
    RF_REG05->BANK_BYTE = 0;

    /* Clear DMA status register */
    Sys_DMA_ClearChannelStatus(standby_mode_env->DMA_channel_RF);

    /* Setup and start the DMA */
    Sys_DMA_ChannelConfig(standby_mode_env->DMA_channel_RF,
                          (DMA_DISABLE_INT_DISABLE  |
                           DMA_ERROR_INT_DISABLE    |
                           DMA_COMPLETE_INT_DISABLE |
                           DMA_COUNTER_INT_DISABLE  |
                           DMA_START_INT_DISABLE    |
                           DMA_DEST_WORD_SIZE_32    |
                           DMA_SRC_WORD_SIZE_32     |
                           DMA_DEST_PBUS            |
                           DMA_PRIORITY_1           |
                           DMA_TRANSFER_M_TO_P      |
                           DMA_SRC_ADDR_INC         |
                           DMA_DEST_ADDR_INC        |
                           DMA_ADDR_LIN             |
                           DMA_ENABLE),
                          sizeof(rf_registers_image_1) / 4,
                          0,
                          (uint32_t) rf_registers_image_1,
                          (uint32_t) RF_BASE);
                          
    /* Wait until DMA is completed */
    while (!(Sys_DMA_Get_ChannelStatus(standby_mode_env->DMA_channel_RF) &
           DMA_COMPLETE_INT_STATUS));                          

    /* Select registers bank 1 */
    RF_REG05->BANK_BYTE = 1;

    /* Manually restore separate words of bank 1 */
    *((uint16_t *) (RF_BASE + 0x06)) = rf_registers_image_2_0x06_0x07;
    *((uint8_t  *) (RF_BASE + 0x08)) = rf_registers_image_2_0x08;
    *((uint8_t  *) (RF_BASE + 0x9F)) = rf_registers_image_2_0x9F;

    /* Setup and start DMA channel to restore registers bank 1
     * in RF front-end from retention memory */

    /* Setup the base addresses for the source and destination */
    DMA->SRC_BASE_ADDR[standby_mode_env->DMA_channel_RF] =
        (uint32_t) rf_registers_image_2_0x1C_0x6F;
    DMA->DEST_BASE_ADDR[standby_mode_env->DMA_channel_RF] =
        (uint32_t) (RF_BASE + 0x1C);

    /* Setup the transfer length and transfer counter interrupt setting */
    DMA->CTRL1[standby_mode_env->DMA_channel_RF] =
        (sizeof(rf_registers_image_2_0x1C_0x6F) / 4) << DMA_CTRL1_TRANSFER_LENGTH_Pos;

    /* Configure the DMA channel */
    DMA->CTRL0[standby_mode_env->DMA_channel_RF] =
                    DMA_DISABLE_INT_DISABLE  |
                    DMA_ERROR_INT_DISABLE    |
                    DMA_COMPLETE_INT_DISABLE |
                    DMA_COUNTER_INT_DISABLE  |
                    DMA_START_INT_DISABLE    |
                    DMA_DEST_WORD_SIZE_32    |
                    DMA_SRC_WORD_SIZE_32     |
                    DMA_DEST_PBUS            |
                    DMA_PRIORITY_1           |
                    DMA_TRANSFER_M_TO_P      |
                    DMA_SRC_ADDR_INC         |
                    DMA_DEST_ADDR_INC        |
                    DMA_ADDR_LIN             |
                    DMA_ENABLE;
                    
    /* Wait until DMA is completed */
    while (!(Sys_DMA_Get_ChannelStatus(standby_mode_env->DMA_channel_RF) &
           DMA_COMPLETE_INT_STATUS));                    

    /* Stop masking interrupts */
    __enable_irq();

    /* Enable the baseband (divider is updated in application if needed) */
    BBIF->CTRL = BB_CLK_ENABLE | BBCLK_DIVIDER_8 | BB_DEEP_SLEEP;

    /* Wait until 48 MHz oscillator is started */
    while (RF_REG39->ANALOG_INFO_CLK_DIG_READY_ALIAS !=
                                  ANALOG_INFO_CLK_DIG_READY_BITBAND);

    /* Switch to (divided 48 MHz) oscillator clock */
    CLK_SYS_CFG->SYSCLK_SRC_SEL_BYTE = SYSCLK_CLKSRC_RFCLK_BYTE;
}
