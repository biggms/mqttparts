#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ConfigManager.h>
//#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFiUdp.h>
#include <AutoUpdate.hpp>
#include <GMQTT.hpp>
#include <MQTTDevices/MQTTSensor.hpp>
#define ONE_WIRE_BUS 4
#define ONE_WIRE_PWR 13
#define WIFI_LED 2
#define RESET_FACTORY 14
#define PREVENT_SLEEP 12

extern "C" {
#include "user_interface.h"
}

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);

const char* theMQTTServer = "192.168.1.59";

ConfigManager configManager;
AutoUpdate* theAutoUpdate;

struct Config {
	int calibration;
	long sleeptime;
	char* mqttserver[ 15 ];
	char* name[ 32 ];
	int mqttport;
} config;

GMQTT theLivingRoomMQ;

bool theFirstRun = true;
long theLastTime = -1000000;

MQTTSensor theTemperatureSensor;

void pulseNotification( int pTimes, long pDuration )
{
	for( int i = 0; i < pTimes * 2; i++ )
	{
		digitalWrite(WIFI_LED, i % 2);
		delay(pDuration);
	}
	digitalWrite(WIFI_LED, HIGH);
}

void createCustomRoute(ESP8266WebServer *server) {
	server->on("/custom", HTTPMethod::HTTP_GET, [server](){
		Serial.println( "Seeeeeeend" );
		server->send(200, "text/plain", "Graham's MQTT Device Test");
	});
}


void setup()
{
	Serial.begin(76800);

	wifi_status_led_uninstall();
	pinMode(WIFI_LED, OUTPUT);
	digitalWrite(WIFI_LED, HIGH);
	pinMode(ONE_WIRE_PWR, OUTPUT);
	digitalWrite(ONE_WIRE_PWR, HIGH);
	pinMode(RESET_FACTORY, INPUT);
	digitalWrite(RESET_FACTORY, HIGH);
	pinMode(PREVENT_SLEEP, INPUT);
	digitalWrite(PREVENT_SLEEP, HIGH);

	pulseNotification(2, 200);

	Serial.println( "GMQTT Device" );

	if( !digitalRead( RESET_FACTORY ) )
	{
		Serial.println( "Factory Reset Pressed - Waiting 10 Seconds" );
		pulseNotification(10, 500);
		digitalWrite(WIFI_LED, HIGH);
		if( !digitalRead( RESET_FACTORY ) )
		{
			Serial.println( "Resetting" );
			EEPROM.begin(200);
			for (int i = 0; i < 200; i++)
			{
				EEPROM.write(i, 0);
			}
			EEPROM.commit();
			Serial.println( "Done - Reboot" );
			ESP.restart();
		}
	}

	config.calibration = 0;
	config.sleeptime = 5;
	config.mqttport = 1883;
	configManager.setAPName("GMQTT");
	configManager.setAPFilename("/index.html");
	configManager.addParameter("calibration", &config.calibration);
	configManager.addParameter("sleeptime", &config.sleeptime);
	configManager.addParameter("mqttserver", (char*)config.mqttserver, 15 );
	configManager.addParameter("name", (char*)config.name, 32 );
	configManager.addParameter("mqttport", &config.mqttport);

	configManager.setAPICallback(createCustomRoute);
	configManager.setAPCallback(createCustomRoute);

	configManager.begin(config);

	if( configManager.isConfigured() )
	{
		DS18B20.requestTemperatures();

		Serial.print( "MQTT on: " );
		Serial.print( (char*)config.mqttserver );
		Serial.print( " port: " );
		Serial.println( config.mqttport );

		String lName = (char*)config.name;

		theLivingRoomMQ.setup( lName, (const char*)config.mqttserver, config.mqttport, true, true );
		theLivingRoomMQ.connect( lName, "biggms", "boldlygo" );


		theTemperatureSensor.setup( &theLivingRoomMQ, lName + "temperature", "temperature/" + lName, "°C" );
		theTemperatureSensor.sendDiscovery();

		theAutoUpdate = new AutoUpdate( "boldlygo", lName.c_str() );
		theAutoUpdate->start();
	}
}

void loop()
{
	bool lValidTemp = false;
	configManager.loop();
	if( configManager.isConfigured() )
	{
		theAutoUpdate->loop();
		theLivingRoomMQ.loop();
		DS18B20.requestTemperatures();
		float temp = DS18B20.getTempCByIndex(0);
		lValidTemp = temp < 85.0f && temp > -127.0f;

		if( lValidTemp && millis() - theLastTime > config.sleeptime * 1000 )
		{
			theLastTime = millis();
			theTemperatureSensor.setValue( String( temp ) );
			theFirstRun = false;
		}

		if( !theFirstRun && digitalRead( PREVENT_SLEEP ) && !theLivingRoomMQ.getStayConnected() )
		{
			Serial.println( "DEEPSLEEP" );
			digitalWrite( ONE_WIRE_PWR, LOW );
			WiFi.forceSleepBegin();
			ESP.deepSleep(config.sleeptime * 1000000, WAKE_RF_DEFAULT);
			delay(1000);
		}
	}
}
