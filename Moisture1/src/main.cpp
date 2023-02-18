/***************************
 * 
 * ttn-esp32 - The Things Network device library for ESP-IDF / SX127x
 * 
 * Copyright (c) 2018 Manuel Bleichenbacher
 * 
 * Licensed under MIT License
 * https://opensource.org/licenses/MIT
 *
 * Sample program showing how to send and receive messages.
 ***************************/

#include "freertos/FreeRTOS.h"
#include "esp_event.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "TheThingsNetwork.h"
#include "esp_log.h"

/* Bibliothek zum Einbinden von Funktionen, die analog eingelesene Signale in digitale umzuwandeln*/
#include "driver/adc.h"

/*Bibliothek zum Einbinden der memcpy-Funktion, um Daten in einen Speicherbereich zu kopieren und 
danach als Byte-Payload an TTN zu übertragen.*/
#include "string.h"

/*Bibliothek zum Einbinden der DeepSleep-Funktion, um den ESP intervallweise ausschalten zu können*/
#include "esp_sleep.h"

extern "C" {
  #include "u8g2_esp32_hal.h"
}

#define TAG_LORA "LoRa"
#define TAG_PROGRAM "Program"
#define TAG_DISPLAY "Display"

// Display PINs
#define SDA_PIN GPIO_NUM_4
#define SCL_PIN GPIO_NUM_15
#define RST_PIN GPIO_NUM_16

//Moisture-Sesnor Pin
#define SENSOR_PIN 32
#define SENSOR_ADC_UNIT ADC_UNIT_1
#define SENSOR_ADC_CHANNEL ADC1_CHANNEL_4

// NOTE:
// The LoRaWAN frequency and the radio chip must be configured by running 'idf.py menuconfig'.
// Go to Components / The Things Network, select the appropriate values and save.

// Copy the below hex strings from the TTN console (Applications > Your application > End devices
// > Your device > Activation information)

// AppEUI (sometimes called JoinEUI)
const char *appEui = "0000000000000000";
// DevEUI
const char *devEui = "70B3D57ED005A2AB";
// AppKey
const char *appKey = "FF2CDAE748B4C28930135F282D64D546";

// Pins and other resources
#define TTN_SPI_HOST      SPI2_HOST
#define TTN_SPI_DMA_CHAN  SPI_DMA_DISABLED
#define TTN_PIN_SPI_SCLK  5
#define TTN_PIN_SPI_MOSI  27
#define TTN_PIN_SPI_MISO  19
#define TTN_PIN_NSS       18
#define TTN_PIN_RXTX      TTN_NOT_CONNECTED
#define TTN_PIN_RST       14
#define TTN_PIN_DIO0      26
#define TTN_PIN_DIO1      35
#define SENSOR_VALUE_MAX  500
#define SENSOR_VALUE_MIN  1200
#define DEEPSLEEP_30MIN   1800000000

static TheThingsNetwork ttn;

/* Initialisierung des Analog-Digital-Converters (ADC). Der ADC wandelt 
analoge Signale, die von einer sensoren oder anderen Quellen kommen, in digitale 
Werte um, die von einem Mikrocontroller verarbeitet werden können.
*/
void init_adc() {
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(SENSOR_ADC_CHANNEL, ADC_ATTEN_DB_0);
}

/* Funktion, die einen Analog-Digital-Konverter (ADC) liest. 
Diese Funktion kann den Wert eines analogen Signals in einen digitalen Wert konvertieren 
*/
int read_adc() {
    return adc1_get_raw(SENSOR_ADC_CHANNEL);
}

/* Die Funktion setup_display() konfiguriert ein Display-Objekt gemäß den angegebenen 
Parametern, wie dem verwendeten Controller-Typ, der Anzahl der Zeilen und Spalten und 
der Art des verwendeten Anschlusses. Nach der Ausführung der Funktion kann das Display 
verwendet werden, um Text und Grafiken darzustellen. 
*/
u8g2_t setup_display() {

	// Reset Pin beim Starten einmal Togglen-----------
	gpio_pad_select_gpio(RST_PIN);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction((gpio_num_t)RST_PIN, GPIO_MODE_OUTPUT);

    /* Reset off (output low) */
    gpio_set_level((gpio_num_t)RST_PIN, 0);
    vTaskDelay(400 / portTICK_RATE_MS);
    /* Reset on (output high) */
    gpio_set_level((gpio_num_t)RST_PIN, 1);

	ESP_LOGI(TAG_PROGRAM, "Reset Pin Toggled\n");
	// -------------------------------------------------

	u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
	u8g2_esp32_hal.sda = SDA_PIN;
	u8g2_esp32_hal.scl = SCL_PIN;
	u8g2_esp32_hal_init(u8g2_esp32_hal);
    vTaskDelay(1);
	u8g2_t u8g2;
	u8g2_Setup_ssd1306_i2c_128x64_noname_f(	// choose the correct setup function
		&u8g2,								// pointer to the u8g2_t variable defined before
		U8G2_R0,							// specifies the display rotation (R0, R1, R2, R3, MIRROR)
		u8g2_esp32_i2c_byte_cb,				// HAL function to send data on the bus
		u8g2_esp32_gpio_and_delay_cb		// HAL function to delay
	);
	u8x8_SetI2CAddress(&u8g2.u8x8,0x78);	// I2C Adress but shifted for some reason
	u8g2_InitDisplay(&u8g2);
	u8g2_SetPowerSave(&u8g2, 0);			// Disable Default Power Save Mode
	u8g2_ClearDisplay(&u8g2);
	u8g2_SendBuffer(&u8g2);
	u8g2_ClearBuffer(&u8g2);
	u8g2_SetFont(&u8g2, u8g2_font_timR14_tf);

	return (u8g2);
}

/* Die Funktion wird verwendet, um eine Nachricht auf dem monochromen Grafik-Display anzuzeigen. 
*/
static void display_message(u8g2_t display, int value){
	u8g2_DrawStr(&display, 2,15, "Feuchtigkeit\n");
	u8g2_DrawStr(&display, 2,55, "Wert:");
	char buf[10];
	snprintf (buf, 9, "%d", value);
	u8g2_DrawStr(&display, 68, 55, buf);
	u8g2_SendBuffer(&display);
	u8g2_ClearBuffer(&display);
	ESP_LOGI(TAG_DISPLAY, "Printed to Display");
}

/* Die Funktion transmitSensorData(int sensor_id, int value) sendet Daten von einem Sensor 
an einen TTN-Server. Die Funktion hat zwei Argumente: sensor_id und value, die den Sensor-ID 
und den Messwert des Sensors repräsentieren. */
void transmitSensorData(int sensor_id, int value) {
    uint8_t payload[4];
    memcpy(payload, &sensor_id, 2);
    memcpy(payload + 2, &value, 2);
    TTNResponseCode res = ttn.transmitMessage(payload, 4);
    printf(res == kTTNSuccessfulTransmission ? "Message sent.\n" : "Transmission failed.\n");
}

/* Die Funktion read_moisture_sendMessages ist eine Funktion in C, die verwendet wird, 
um Feuchtigkeitsdaten von einem ADC (Analog-Digital-Converter) zu lesen, zu berechnen und zu übertragen. */
void read_moisture_sendMessages(void* pvParameter, u8g2_t display)
{  
    int moisture;
    int dividend;
    float temp;

    while (1) {
        ESP_LOGI(TAG_LORA, "Sending message...");
        
        moisture = read_adc();                                      //Feuchtigkeitsdaten von einer ADC-Funktion read_adc lesen  
        moisture = moisture - SENSOR_VALUE_MAX;
        dividend = SENSOR_VALUE_MIN - moisture;
        temp = ((float)dividend / (float)SENSOR_VALUE_MIN);
        temp = (temp * 100);                                        //Feuchtigkeitsdaten werden in Prozent angezeigt
        moisture = (int)temp;

        ESP_LOGI("MOISTURE", "Moisture level: %d", moisture);          
        display_message(display, moisture);                         //berechneten Feuchtigkeitsdaten werden auf einem Display mit der Funktion
        
        transmitSensorData(1, moisture);                            //Sensor_ID und gemessener Wert wird über transmitSensorData an TTN übertragen                   
        
        esp_sleep_enable_timer_wakeup(DEEPSLEEP_30MIN);             // ESP32 wird nach 30 Minuten aufwachen
        esp_deep_sleep_start();                                     // Setzen Sie den ESP32 in den Deep Sleep-Modus
    }
}

/* MessageReceived ist eine C-Funktion, die Nachrichten empfängt, die über ein 
bestimmtes Netzwerkprotokoll wie TheThingsNetwork gesendet werden. */
void messageReceived(const uint8_t* message, size_t length, ttn_port_t port)
{
    printf("Message of %d bytes received on port %d:", length, port);
    for (int i = 0; i < length; i++)
        printf(" %02x", message[i]);
    printf("\n");
}

extern "C" void app_main(void)
{
    init_adc();

    u8g2_t u8g2 = setup_display();
    ESP_LOGI(TAG_PROGRAM, "Setup Finished");
    esp_err_t err;
    // Initialize the GPIO ISR handler service
    err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    ESP_ERROR_CHECK(err);
    
    // Initialize the NVS (non-volatile storage) for saving and restoring the keys
    err = nvs_flash_init();
    ESP_ERROR_CHECK(err);

    // Initialize SPI bus
    spi_bus_config_t spi_bus_config;
    spi_bus_config.miso_io_num = TTN_PIN_SPI_MISO;
    spi_bus_config.mosi_io_num = TTN_PIN_SPI_MOSI;
    spi_bus_config.sclk_io_num = TTN_PIN_SPI_SCLK;
    spi_bus_config.quadwp_io_num = -1;
    spi_bus_config.quadhd_io_num = -1;
    spi_bus_config.max_transfer_sz = 0;
    spi_bus_config.intr_flags = 0;
    err = spi_bus_initialize(TTN_SPI_HOST, &spi_bus_config, TTN_SPI_DMA_CHAN);
    ESP_ERROR_CHECK(err);

    // Configure the SX127x pins
    ttn.configurePins(TTN_SPI_HOST, TTN_PIN_NSS, TTN_PIN_RXTX, TTN_PIN_RST, TTN_PIN_DIO0, TTN_PIN_DIO1);

    // The below line can be commented after the first run as the data is saved in NVS
    ttn.provision(devEui, appEui, appKey);

    // Register callback for received messages
    ttn.onMessage(messageReceived);

//    ttn.setAdrEnabled(false);
//    ttn.setDataRate(kTTNDataRate_US915_SF7);
//    ttn.setMaxTxPower(14);

    printf("Joining...\n");
    if (ttn.join())
    {
        printf("Joined.\n");
        while (1){
            read_moisture_sendMessages((void* )0, u8g2);
        }
    }
    else
    {
        printf("Join failed. Goodbye\n");
    }
}

