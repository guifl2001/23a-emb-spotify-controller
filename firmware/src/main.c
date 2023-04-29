/************************************************************************
* 5 semestre - Eng. da Computao - Insper
*
* 2021 - Exemplo com HC05 com RTOS
*
*/

#include <asf.h>
#include "conf_board.h"
#include <string.h>

/************************************************************************/
/* defines                                                              */
/************************************************************************/

// LEDs
#define LED_PIO      PIOC
#define LED_PIO_ID   ID_PIOC
#define LED_IDX      8
#define LED_IDX_MASK (1 << LED_IDX)

// Botão
#define BUT_PIO      PIOA
#define BUT_PIO_ID   ID_PIOA
#define BUT_IDX      11
#define BUT_IDX_MASK (1 << BUT_IDX)

#define BUT_VERDE_PIO             	PIOB
#define BUT_VERDE_PIO_ID          	ID_PIOB
#define BUT_VERDE_PIO_IDX         	3
#define BUT_VERDE_PIO_IDX_MASK      (1 << BUT_VERDE_PIO_IDX)

#define BUT_VERMELHO_PIO            PIOA
#define BUT_VERMELHO_PIO_ID         ID_PIOA
#define BUT_VERMELHO_PIO_IDX        0
#define BUT_VERMELHO_PIO_IDX_MASK   (1 << BUT_VERMELHO_PIO_IDX)

#define BUT_AZUL_PIO              PIOC
#define BUT_AZUL_PIO_ID           ID_PIOC
#define BUT_AZUL_PIO_IDX          31
#define BUT_AZUL_PIO_IDX_MASK     (1 << BUT_AZUL_PIO_IDX)

#define AFEC_POT0 AFEC0
#define AFEC_POT0_ID ID_AFEC0
#define AFEC_POT0_CHANNEL 0 // Canal do pino PD30

// usart (bluetooth ou serial)
// Descomente para enviar dados
// pela serial debug

//#define DEBUG_SERIAL

#ifdef DEBUG_SERIAL
#define USART_COM USART1
#define USART_COM_ID ID_USART1
#else
#define USART_COM USART0
#define USART_COM_ID ID_USART0
#endif

/************************************************************************/
/* RTOS                                                                 */
/************************************************************************/

#define TASK_BLUETOOTH_STACK_SIZE            (4096/sizeof(portSTACK_TYPE))
#define TASK_BLUETOOTH_STACK_PRIORITY        (tskIDLE_PRIORITY)

#define TASK_ADC_STACK_SIZE (1024 * 10 / sizeof(portSTACK_TYPE))
#define TASK_ADC_STACK_PRIORITY (tskIDLE_PRIORITY)


#define TASK_HANDSHAKE_STACK_SIZE            (4096/sizeof(portSTACK_TYPE))
#define TASK_HANDSHAKE_STACK_PRIORITY        (tskIDLE_PRIORITY)

/************************************************************************/
/* prototypes                                                           */
/************************************************************************/
QueueHandle_t xQueueButton;
QueueHandle_t xQueueADC;
TimerHandle_t xTimer;

TaskHandle_t xTaskBluetoothHandle;
TaskHandle_t xTaskHandshakeHandle;

typedef struct {
  uint value;
} ADCData;

typedef struct {
	char tipo; 
	int value;
} pack;

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask,
signed char *pcTaskName);
extern void vApplicationIdleHook(void);
extern void vApplicationTickHook(void);
extern void vApplicationMallocFailedHook(void);
extern void xPortSysTickHandler(void);


static void config_AFEC_pot(Afec *afec, uint32_t afec_id, uint32_t afec_channel,
                            afec_callback_t callback);
void task_bluetooth(void);

/************************************************************************/
/* constants                                                            */
/************************************************************************/

/************************************************************************/
/* variaveis globais                                                    */
/************************************************************************/

/************************************************************************/
/* RTOS application HOOK                                                */
/************************************************************************/

/* Called if stack overflow during execution */
extern void vApplicationStackOverflowHook(xTaskHandle *pxTask,
signed char *pcTaskName) {
	printf("stack overflow %x %s\r\n", pxTask, (portCHAR *)pcTaskName);
	/* If the parameters have been corrupted then inspect pxCurrentTCB to
	* identify which task has overflowed its stack.
	*/
	for (;;) {
	}
}

/* This function is called by FreeRTOS idle task */
extern void vApplicationIdleHook(void) {
	pmc_sleep(SAM_PM_SMODE_SLEEP_WFI);
}

/* This function is called by FreeRTOS each tick */
extern void vApplicationTickHook(void) { }

extern void vApplicationMallocFailedHook(void) {
	/* Called if a call to pvPortMalloc() fails because there is insufficient
	free memory available in the FreeRTOS heap.  pvPortMalloc() is called
	internally by FreeRTOS API functions that create tasks, queues, software
	timers, and semaphores.  The size of the FreeRTOS heap is set by the
	configTOTAL_HEAP_SIZE configuration constant in FreeRTOSConfig.h. */

	/* Force an assert. */
	configASSERT( ( volatile void * ) NULL );
}

/************************************************************************/
/* handlers / callbacks                                                 */
/************************************************************************/

/************************************************************************/
/* funcoes                                                              */
/************************************************************************/
void but_verde_callback(void){
	pack p;
	p.tipo = 'b';
	p.value = 1;
	
	xQueueSendFromISR(xQueueButton, &p, 0);
}
void but_vermelho_callback(void){
	pack p;
	p.tipo = 'b';
	p.value = 2;
	xQueueSendFromISR(xQueueButton, &p, 0);
}
void but_azul_callback(void){
	pack p;
	p.tipo = 'b';
	p.value = 3;
	xQueueSendFromISR(xQueueButton, &p, 0);
}

static void AFEC_pot0_callback(void) {
    ADCData adc;
    adc.value = afec_channel_get_value(AFEC_POT0, AFEC_POT0_CHANNEL);
    BaseType_t xHigherPriorityTaskWoken = pdTRUE;
    xQueueSendFromISR(xQueueADC, &adc, &xHigherPriorityTaskWoken);
}



void io_init(void) {

// Ativa PIOs
	pmc_enable_periph_clk(ID_PIOA);
	pmc_enable_periph_clk(ID_PIOC);
	pmc_enable_periph_clk(ID_PIOD);
	pmc_enable_periph_clk(ID_PIOB);
	
	// Configura Pinos
	pio_configure(LED_PIO, PIO_OUTPUT_1, LED_IDX_MASK, PIO_DEFAULT | PIO_DEBOUNCE);
	pio_configure(BUT_PIO, PIO_INPUT, BUT_IDX_MASK, PIO_PULLUP);

	pio_configure(BUT_VERDE_PIO, PIO_INPUT, BUT_VERDE_PIO_IDX_MASK, PIO_PULLUP| PIO_DEBOUNCE);
	pio_set_debounce_filter(BUT_VERDE_PIO, BUT_VERDE_PIO_IDX_MASK, 60);
	pio_configure(BUT_VERMELHO_PIO, PIO_INPUT, BUT_VERMELHO_PIO_IDX_MASK, PIO_PULLUP| PIO_DEBOUNCE);
	pio_set_debounce_filter(BUT_VERMELHO_PIO, BUT_VERMELHO_PIO_IDX_MASK, 60);
	pio_configure(BUT_AZUL_PIO, PIO_INPUT, BUT_AZUL_PIO_IDX_MASK, PIO_PULLUP| PIO_DEBOUNCE);
	pio_set_debounce_filter(BUT_AZUL_PIO, BUT_AZUL_PIO_IDX_MASK, 60);

		
	pio_handler_set(BUT_VERDE_PIO, BUT_VERDE_PIO_ID, BUT_VERDE_PIO_IDX_MASK, PIO_IT_FALL_EDGE,
		but_verde_callback);
			
	pio_handler_set(BUT_VERMELHO_PIO, BUT_VERMELHO_PIO_ID, BUT_VERMELHO_PIO_IDX_MASK, PIO_IT_FALL_EDGE,
		but_vermelho_callback);
	
	pio_handler_set(BUT_AZUL_PIO, BUT_AZUL_PIO_ID, BUT_AZUL_PIO_IDX_MASK, PIO_IT_FALL_EDGE,
		but_azul_callback);
	
	pio_enable_interrupt(BUT_VERDE_PIO, BUT_VERDE_PIO_IDX_MASK);
	pio_enable_interrupt(BUT_VERMELHO_PIO, BUT_VERMELHO_PIO_IDX_MASK);
	pio_enable_interrupt(BUT_AZUL_PIO, BUT_AZUL_PIO_IDX_MASK);
	
	pio_get_interrupt_status(BUT_VERDE_PIO);
	pio_get_interrupt_status(BUT_VERMELHO_PIO);
	pio_get_interrupt_status(BUT_AZUL_PIO);


	// coloca pino em 0

	NVIC_EnableIRQ(BUT_VERDE_PIO_ID);
	NVIC_SetPriority(BUT_VERDE_PIO_ID, 4);

	NVIC_EnableIRQ(BUT_AZUL_PIO_ID);
	NVIC_SetPriority(BUT_AZUL_PIO_ID, 4);
	
	NVIC_EnableIRQ(BUT_VERMELHO_PIO_ID);
	NVIC_SetPriority(BUT_VERMELHO_PIO_ID, 4);
}

static void config_AFEC_pot(Afec *afec, uint32_t afec_id, uint32_t afec_channel,
                            afec_callback_t callback) {
    /*************************************
     * Ativa e configura AFEC
     *************************************/
    /* Ativa AFEC - 0 */
    afec_enable(afec);

    /* struct de configuracao do AFEC */
    struct afec_config afec_cfg;

    /* Carrega parametros padrao */
    afec_get_config_defaults(&afec_cfg);

    /* Configura AFEC */
    afec_init(afec, &afec_cfg);

    /* Configura trigger por software */
    afec_set_trigger(afec, AFEC_TRIG_SW);

    /*** Configuracao específica do canal AFEC ***/
    struct afec_ch_config afec_ch_cfg;
    afec_ch_get_config_defaults(&afec_ch_cfg);
    afec_ch_cfg.gain = AFEC_GAINVALUE_0;
    afec_ch_set_config(afec, afec_channel, &afec_ch_cfg);

    /*
    * Calibracao:
    * Because the internal ADC offset is 0x200, it should cancel it and shift
    down to 0.
    */
    afec_channel_set_analog_offset(afec, afec_channel, 0x200);

    /***  Configura sensor de temperatura ***/
    struct afec_temp_sensor_config afec_temp_sensor_cfg;

    afec_temp_sensor_get_config_defaults(&afec_temp_sensor_cfg);
    afec_temp_sensor_set_config(afec, &afec_temp_sensor_cfg);

    /* configura IRQ */
    afec_set_callback(afec, afec_channel, callback, 1);
    NVIC_SetPriority(afec_id, 4);
    NVIC_EnableIRQ(afec_id);
}

static void configure_console(void) {
	const usart_serial_options_t uart_serial_options = {
		.baudrate = CONF_UART_BAUDRATE,
		#if (defined CONF_UART_CHAR_LENGTH)
		.charlength = CONF_UART_CHAR_LENGTH,
		#endif
		.paritytype = CONF_UART_PARITY,
		#if (defined CONF_UART_STOP_BITS)
		.stopbits = CONF_UART_STOP_BITS,
		#endif
	};

	/* Configure console UART. */
	stdio_serial_init(CONF_UART, &uart_serial_options);

	/* Specify that stdout should not be buffered. */
	#if defined(__GNUC__)
	setbuf(stdout, NULL);
	#else
	/* Already the case in IAR's Normal DLIB default configuration: printf()
	* emits one character at a time.
	*/
	#endif
}

uint32_t usart_puts(uint8_t *pstring) {
	uint32_t i ;

	while(*(pstring + i))
	if(uart_is_tx_empty(USART_COM))
	usart_serial_putchar(USART_COM, *(pstring+i++));
}

void usart_put_string(Usart *usart, char str[]) {
	usart_serial_write_packet(usart, str, strlen(str));
}

int usart_get_string(Usart *usart, char buffer[], int bufferlen, uint timeout_ms) {
	uint timecounter = timeout_ms;
	uint32_t rx;
	uint32_t counter = 0;

	while( (timecounter > 0) && (counter < bufferlen - 1)) {
		if(usart_read(usart, &rx) == 0) {
			buffer[counter++] = rx;
		}
		else{
			timecounter--;
			vTaskDelay(1);
		}
	}
	buffer[counter] = 0x00;
	return counter;
}

void usart_send_command(Usart *usart, char buffer_rx[], int bufferlen,
char buffer_tx[], int timeout) {
	usart_put_string(usart, buffer_tx);
	usart_get_string(usart, buffer_rx, bufferlen, timeout);
}

void config_usart0(void) {
	sysclk_enable_peripheral_clock(ID_USART0);
	usart_serial_options_t config;
	config.baudrate = 9600;
	config.charlength = US_MR_CHRL_8_BIT;
	config.paritytype = US_MR_PAR_NO;
	config.stopbits = false;
	usart_serial_init(USART0, &config);
	usart_enable_tx(USART0);
	usart_enable_rx(USART0);

	// RX - PB0  TX - PB1
	pio_configure(PIOB, PIO_PERIPH_C, (1 << 0), PIO_DEFAULT);
	pio_configure(PIOB, PIO_PERIPH_C, (1 << 1), PIO_DEFAULT);
}

int hc05_init(void) {
	char buffer_rx[128];
	usart_send_command(USART_COM, buffer_rx, 1000, "AT", 100);
	vTaskDelay( 500 / portTICK_PERIOD_MS);
	usart_send_command(USART_COM, buffer_rx, 1000, "AT", 100);
	vTaskDelay( 500 / portTICK_PERIOD_MS);
	usart_send_command(USART_COM, buffer_rx, 1000, "AT+NAMEagoravai", 100);
	vTaskDelay( 500 / portTICK_PERIOD_MS);
	usart_send_command(USART_COM, buffer_rx, 1000, "AT", 100);
	vTaskDelay( 500 / portTICK_PERIOD_MS);
	usart_send_command(USART_COM, buffer_rx, 1000, "AT+PIN0000", 100);
}

/************************************************************************/
/* TASKS                                                                */
/************************************************************************/

void vTimerCallback0(TimerHandle_t xTimer) {
    /* Selecina canal e inicializa conversão */
    afec_channel_enable(AFEC_POT0, AFEC_POT0_CHANNEL);
    afec_start_software_conversion(AFEC_POT0);
}

static void task_adc(void *pvParameters) {

    // configura ADC e TC para controlar a leitura
    config_AFEC_pot(AFEC_POT0, AFEC_POT0_ID, AFEC_POT0_CHANNEL, AFEC_pot0_callback);

    xTimer = xTimerCreate("Timer0", 100, pdTRUE, (void *)0, vTimerCallback0);
    xTimerStart(xTimer, 0);

    ADCData adc;
	pack type;
    while (1) {
        if (xQueueReceive(xQueueADC, &(adc), 0)) {
			type.tipo = 'a';
			type.value = adc.value;
			xQueueSend(xQueueButton, &type, 0);
        }
    }
}

void task_handshake(void){
	io_init();

	config_usart0();
	hc05_init();
	
	char eof = 'X';
	char handshake;
	pack hand;
	hand.tipo = 'h';
	hand.value = 0;
	while (1) {
		pio_set(LED_PIO, LED_IDX_MASK);
		vTaskDelay(100 / portTICK_PERIOD_MS);
		pio_clear(LED_PIO, LED_IDX_MASK);
		vTaskDelay(100 / portTICK_PERIOD_MS);

		if (usart_read(USART_COM, &handshake) == 0) {
			if (handshake == 'h') {
				pio_clear(LED_PIO, LED_IDX_MASK);
				xTaskCreate(task_bluetooth, "BLT", TASK_BLUETOOTH_STACK_SIZE, NULL,	TASK_BLUETOOTH_STACK_PRIORITY, NULL);
			}
		}

		while(!usart_is_tx_ready(USART_COM)) {
			vTaskDelay(10 / portTICK_PERIOD_MS);
		}
		usart_write(USART_COM, hand.tipo);

		
		while(!usart_is_tx_ready(USART_COM)) {
			vTaskDelay(10 / portTICK_PERIOD_MS);
		}
		usart_write(USART_COM, hand.value);

		while(!usart_is_tx_ready(USART_COM)) {
			vTaskDelay(10 / portTICK_PERIOD_MS);
		}
		usart_write(USART_COM, hand.value >> 8);

		while(!usart_is_tx_ready(USART_COM)) {
			vTaskDelay(10 / portTICK_PERIOD_MS);
		}
		usart_write(USART_COM, eof);

	}	

}

void task_bluetooth(void) {
	printf("Task Bluetooth started \n");
	
	printf("Inicializando HC05 \n");
	config_usart0();
	hc05_init();

	// configura LEDs e Botões
	io_init();

	char button1 = '0';
	char eof = 'X';

	// Task não deve retornar.
	while(1) {
		// atualiza valor do botão
		if(pio_get(BUT_PIO, PIO_INPUT, BUT_IDX_MASK) == 0) {
			button1 = '1';
		} else {
			button1 = '0';
		}

		// envia status botão
		while(!usart_is_tx_ready(USART_COM)) {
			vTaskDelay(10 / portTICK_PERIOD_MS);
		}
		usart_write(USART_COM, button1);
		
		// envia fim de pacote
		while(!usart_is_tx_ready(USART_COM)) {
			vTaskDelay(10 / portTICK_PERIOD_MS);
		}
		usart_write(USART_COM, eof);

		// dorme por 500 ms
		vTaskDelay(500 / portTICK_PERIOD_MS);
	}
}

/************************************************************************/
/* main                                                                 */
/************************************************************************/

int main(void) {
	/* Initialize the SAM system */
	sysclk_init();
	board_init();

	configure_console();
  
  	xQueueADC = xQueueCreate(100, sizeof(adcData));
    if (xQueueADC == NULL)
        printf("falha em criar a queue xQueueADC1 \n");
        
    if (xTaskCreate(task_adc, "ADC", TASK_ADC_STACK_SIZE, NULL,
                    TASK_ADC_STACK_PRIORITY, NULL) != pdPASS) {
        printf("Failed to create test ADC task\r\n");
    }
	xTaskCreate(task_handshake, "Handshake", TASK_HANDSHAKE_STACK_SIZE, NULL,
					TASK_HANDSHAKE_STACK_PRIORITY, &xTaskHandshakeHandle);
	xQueueButton =  xQueueCreate(32, sizeof(pack));
	/* Start the scheduler. */
	vTaskStartScheduler();

	while(1){}

	/* Will only get here if there was insufficient memory to create the idle task. */
	return 0;
}
