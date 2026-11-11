/*Power*/
#define VEXT_ENABLE 18
#define VEXT_ON_VALUE LOW
#ifndef PIN_PERIPH_POWER
#define PIN_PERIPH_POWER 18
#endif
#define PIN_GPS_EN 11
#ifndef PIN_GPS_SWITCH
#define PIN_GPS_SWITCH 11
#endif
#define GPS_EN_ACTIVE LOW

/*Wire Interface*/
#define WIRE_INTERFACES_COUNT 2
// I2C keybuttons
#define I2C_SCL1 21
#define I2C_SDA1 20
#define KB_INT 12
#ifndef PIN_KB_WAKEUP
#define PIN_KB_WAKEUP 12
#endif
// I2C peripheral`
#define I2C_SCL 6
#define I2C_SDA 7

/*LED*/
// Note: GPIO13 is BAT_ADC (PIN_VBAT_READ), not a status LED.
#define KB_LED 46
#ifndef PIN_KEY_LED
#define PIN_KEY_LED 46
#endif

/*BUZZER*/
#define PIN_BUZZER 9
#ifndef THINKNODE_M9_BUZZER_PIN
#define THINKNODE_M9_BUZZER_PIN 9
#endif

/*CHARGE_CHECK*/
#define DONE 8
#ifndef PIN_CHG_DONE
#define PIN_CHG_DONE 8
#endif
#define EXT_PWR_DETECT 1
#ifndef PIN_USB_5V_SENSE
#define PIN_USB_5V_SENSE 1
#endif
#define EXT_CHRG_DETECT 1
#define EXT_CHRG_DETECT_VALUE LOW
/*GPS*/
#define GPS_L76K
#define GPS_BAUDRATE 9600
#define PIN_GPS_RESET 5
#define PIN_GPS_PPS 4
#ifndef PIN_GPS_STANDBY
#define PIN_GPS_STANDBY 10 // An output to wake GPS, low means allow sleep, high means force wake
#endif
#define GPS_TX_PIN 3
#define GPS_RX_PIN 2
#define GPS_THREAD_INTERVAL 50

/*SPI*/
#define SPI_MOSI 47
#define SPI_SCK 40
#define SPI_MISO 38

/*SD Card*/
#define SDCARD_CS 48
#ifndef PIN_SD_CS
#define PIN_SD_CS 48
#endif
#define SD_SPI_FREQUENCY 80000000U

/*Screen*/
// #define USE_ST7789 1
#define ST7789_CS 16
#ifndef PIN_TFT_CS
#define PIN_TFT_CS 16
#endif
#define ST7789_RS 15
#define ST7789_TE 19
#define ST7789_SDA SPI_MOSI // MOSI
#define ST7789_SCK SPI_SCK
#define ST7789_RESET 14
#define ST7789_MISO SPI_MISO
#define ST7789_BUSY -1
#define ST7789_BL 17
#ifndef PIN_TFT_BL_EN
#define PIN_TFT_BL_EN 17
#endif
#define ST7789_SPI_HOST SPI2_HOST
#define SPI_FREQUENCY 80000000
#define SPI_READ_FREQUENCY 16000000

#define USE_TFTDISPLAY 1
#define TFT_CS ST7789_CS
#define TFT_BL ST7789_BL
#define TFT_HEIGHT 320
#define TFT_WIDTH 240
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define TFT_OFFSET_ROTATION 0
#define TFT_PWM_FREQ 44000
#define TFT_PWM_CHANNEL 7
#define TFT_INVERT_LIGHT true
#define TFT_BACKLIGHT_ON LOW
#define SCREEN_ROTATE
#define SCREEN_TRANSITION_FRAMERATE 5
#define BRIGHTNESS_DEFAULT 128

#define LGFX_PANEL ST7789
#define DISPLAY_SIZE 320x240
#define LGFX_ROTATION 0
#define LGFX_CFG_HOST SPI2_HOST
#define LGFX_PIN_SCK ST7789_SCK
#define LGFX_PIN_MOSI ST7789_SDA
#define LGFX_PIN_DC ST7789_RS
#define LGFX_PIN_CS ST7789_CS
#define LGFX_PIN_BL ST7789_BL
#define LGFX_SCREEN_WIDTH TFT_WIDTH
#define LGFX_SCREEN_HEIGHT TFT_HEIGHT
#define LGFX_INVERT_LIGHT true
#define LGFX_PWM_FREQ 44000
#define LGFX_PWM_CHANNEL 7

/*Lora radio*/
#define LORA_SCK SPI_SCK
#define LORA_MISO SPI_MISO
#define LORA_MOSI SPI_MOSI
#define LORA_CS 39
#define LORA_RESET 45
#define LORA_DIO0 41

#define USE_LR1110
#define LR1110_IRQ_PIN 42
#define LR1110_NRESET_PIN LORA_RESET
#define LR1110_BUSY_PIN LORA_DIO0
#define LR1110_SPI_NSS_PIN LORA_CS
#define LR1110_SPI_SCK_PIN LORA_SCK
#define LR1110_SPI_MOSI_PIN LORA_MOSI
#define LR1110_SPI_MISO_PIN LORA_MISO
#define LR11X0_DIO3_TCXO_VOLTAGE 3.3
#define LR11X0_DIO_AS_RF_SWITCH

/*RTC*/
#define PCF8563_RTC 0x51
