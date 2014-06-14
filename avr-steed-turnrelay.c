/*
 * avr_steed_turnrelay.c
 *
 * Created: 13.06.2014 22:33:42
 *  Author: Trol
 */ 

#define F_CPU							9600000
#define DEFAULT_BUZZER_DURATION			5
#define LIGHT_DUTY_CYCLE				50			// in percent
#define DEFAULT_LIGHT_PERIOD			800			// in milliseconds

// Limit values (in milliseconds)
#define MIN_LIGHT_PERIOD				500
#define MAX_LIGHT_PERIOD				1000
#define MIN_BUZZER_DURATION				0
#define MAX_BUZZER_DURATION				250

// EEPROM offsets
#define ADDR_LIGHT_PERIOD				0
#define ADDR_BUZZER_DURATION			2

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <avr/eeprom.h> 
#include <stdbool.h>
#include <util/delay.h>


#define PIN_LIGHT_OUT		PB0
#define PIN_BUZZER_OUT		PB1
#define PIN_BUTTON_PLUS		PB3
#define PIN_BUTTON_MINUS	PB4


volatile uint8_t timer0deltaCountA;
volatile uint8_t timer0deltaCountB;
volatile bool soundEnabled;


volatile uint8_t buzzerDuration;	// in milliseconds
volatile uint16_t lightPeriod;
volatile uint16_t lightPeriodOn;

#define SETTINGS_MODE_NONE		0
#define SETTINGS_MODE_LIGHT		1
#define SETTINGS_MODE_SOUND		2


ISR(TIM0_COMPA_vect) {
	OCR0A += timer0deltaCountA;
	
	static uint16_t counter = 0;
	counter++;

	if (counter < lightPeriodOn) {
		PORTB |= _BV(PIN_LIGHT_OUT);
		soundEnabled = counter < buzzerDuration;
	} else {
		PORTB &= ~_BV(PIN_LIGHT_OUT);
		soundEnabled = counter-lightPeriodOn < buzzerDuration;
	}
	if (counter >= lightPeriod) {
		lightPeriodOn = lightPeriod*LIGHT_DUTY_CYCLE/100;
		counter = 0;
	}
	wdt_reset();
}


ISR(TIM0_COMPB_vect) {
	OCR0B += timer0deltaCountB;
	
	if (soundEnabled) {
		PORTB ^= _BV(PIN_BUZZER_OUT);
	} else {
		PORTB &= ~_BV(PIN_BUZZER_OUT);
	}
}


int main(void) {
	wdt_enable(WDTO_30MS);
	// Set up pins
	DDRB = _BV(PIN_LIGHT_OUT) | _BV(PIN_BUZZER_OUT);

	// Set up TC0	
	timer0deltaCountA = F_CPU/64/1000;	// interrupt will be called 1000 times per second
	timer0deltaCountB = F_CPU/64/1000;	// interrupt will be called 1000 times per second
	OCR0A = timer0deltaCountA;
	OCR0B = timer0deltaCountB;
	TCCR0A = 0;
	TCCR0B = _BV(CS01) | _BV(CS00);	// x64 divider
	TIMSK0 |= _BV(OCIE0A) | _BV(OCIE0B);
	TIFR0 |= _BV(OCF0A) | _BV(OCF0B);
	TCNT0 = 0;
	
	sei();
	
	// Set up parameters
	buzzerDuration = DEFAULT_BUZZER_DURATION;
	lightPeriod = DEFAULT_LIGHT_PERIOD;
	lightPeriodOn = lightPeriod*LIGHT_DUTY_CYCLE/100;
	
	// Load from EEPROM
	_delay_ms(27);		// wait to stabilize the power
	buzzerDuration = eeprom_read_byte((uint8_t*)ADDR_BUZZER_DURATION);
	if (buzzerDuration < MIN_BUZZER_DURATION || buzzerDuration > MAX_BUZZER_DURATION) {
		buzzerDuration = DEFAULT_BUZZER_DURATION;
	}
	lightPeriod = eeprom_read_word((uint16_t*)ADDR_LIGHT_PERIOD);
	if (lightPeriod < MIN_LIGHT_PERIOD || lightPeriod > MAX_LIGHT_PERIOD) {
		lightPeriod = DEFAULT_LIGHT_PERIOD;
	}
	lightPeriodOn = lightPeriod*LIGHT_DUTY_CYCLE/100;
	
	// keyboard cycle
	uint8_t plusPressedCount = 0;
	uint8_t minusPressedCount = 0;
	uint8_t settingsMode = SETTINGS_MODE_NONE;
    while (true) {
		PORTB |= _BV(PIN_BUTTON_MINUS) | _BV(PIN_BUTTON_PLUS);
		if (PINB & _BV(PIN_BUTTON_MINUS)) {
			minusPressedCount = 0;
		} else if (minusPressedCount < 0xff) {
			minusPressedCount++;
		}
		if (PINB & _BV(PIN_BUTTON_PLUS)) {
			plusPressedCount = 0;
		} else if (plusPressedCount < 0xff) {
			plusPressedCount++;
		}
		_delay_ms(10);
		
		// Check settings mode changed
		if (minusPressedCount >= 200 && plusPressedCount >= 200) {
			minusPressedCount = 0;
			plusPressedCount = 0;
			settingsMode++;
			if (settingsMode > SETTINGS_MODE_SOUND) {
				settingsMode = SETTINGS_MODE_NONE;
				// save settings if need
				uint8_t storedBuzzerDuration = eeprom_read_byte((uint8_t*)ADDR_BUZZER_DURATION);
				if (storedBuzzerDuration != buzzerDuration) {
					eeprom_write_byte((uint8_t*)ADDR_BUZZER_DURATION, buzzerDuration);
				}
				uint16_t storedLightPeriod = eeprom_read_word((uint16_t*)ADDR_LIGHT_PERIOD);
				if (storedLightPeriod != lightPeriod) {
					eeprom_write_word((uint16_t*)ADDR_LIGHT_PERIOD, lightPeriod);
				}
			}
		}
		if (settingsMode == SETTINGS_MODE_NONE) {
			continue;
		}
		if (minusPressedCount > 0 && plusPressedCount > 0) {
			continue;
		}
		// check menu keyboard commands
		if (minusPressedCount > 10) {
			switch (settingsMode) {
				case SETTINGS_MODE_LIGHT:
					if (lightPeriod < MAX_LIGHT_PERIOD) {
						lightPeriod ++;
					}
					break;
				case SETTINGS_MODE_SOUND:
					if (buzzerDuration > MIN_BUZZER_DURATION) {
						buzzerDuration--;
					}
					break;
			}
		} else if (plusPressedCount > 10) {
			switch (settingsMode) {
				case SETTINGS_MODE_LIGHT:
					if (lightPeriod > MIN_LIGHT_PERIOD) {
						lightPeriod --;
					}
					break;
				case SETTINGS_MODE_SOUND:
					if (buzzerDuration < MAX_BUZZER_DURATION) {
						buzzerDuration++;
					}
					break;
			}
		}
			
    }
}