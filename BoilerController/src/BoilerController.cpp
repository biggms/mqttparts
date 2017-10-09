#include <Arduino.h>
#include <Boiler.hpp>
#include <ESP8266WiFi.h>
#include <ConfigManager.h>
#include <AutoUpdate.hpp>
#include <GMQTT.hpp>
#include <MQTTDevices/MQTTSwitch.hpp>
#include <MQTTDevices/MQTTBinarySensor.hpp>
#define RESET_FACTORY 14
#define PREVENT_SLEEP 12
#define MAX_ROOMS 32

extern "C" {
#include "user_interface.h"
}

MQTTSwitch theMasterSwitch;
MQTTBinarySensor theDemandMaster;

Boiler theBoiler( 5, false );
AutoUpdate* theAutoUpdate;

ConfigManager configManager;

struct Config {
	char* mqttserver[ 15 ];
	char* name[ 32 ];
	int mqttport;
} config;

GMQTT theBoilerRoomMQ;

bool theFirstRun = true;

MQTTSwitch theRoomSwitches[ MAX_ROOMS ];

void createCustomRoute(ESP8266WebServer *server) {
	server->on("/custom", HTTPMethod::HTTP_GET, [server](){
		server->send(200, "text/plain", "GMQTT Bolier");
	});
}


void setup()
{
	Serial.begin(76800);

	pinMode(RESET_FACTORY, INPUT);
	digitalWrite(RESET_FACTORY, HIGH);
	pinMode(PREVENT_SLEEP, INPUT);
	digitalWrite(PREVENT_SLEEP, HIGH);
	pinMode(BUILTIN_LED, OUTPUT);

	Serial.println( "GMQTT Boiler Controller" );

	if( !digitalRead( RESET_FACTORY ) )
	{
		Serial.println( "Factory Reset Pressed - Waiting 10 Seconds" );
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

	config.mqttport = 1883;
	configManager.setAPName("GMQTT");
	configManager.setAPFilename("/index.html");
	configManager.addParameter("mqttserver", (char*)config.mqttserver, 15 );
	configManager.addParameter("name", (char*)config.name, 32 );
	configManager.addParameter("mqttport", &config.mqttport);

	configManager.setAPICallback(createCustomRoute);
	configManager.setAPCallback(createCustomRoute);

	configManager.begin(config);

	if( configManager.isConfigured() )
	{

		Serial.print( "MQTT on: " );
		Serial.print( (char*)config.mqttserver );
		Serial.print( " port: " );
		Serial.println( config.mqttport );

		String lName = (char*)config.name;

		theBoilerRoomMQ.setup( lName, (const char*)config.mqttserver, config.mqttport, true, true );
		theBoilerRoomMQ.connect( lName, "biggms", "boldlygo" );

		theMasterSwitch.setup( &theBoilerRoomMQ, "boilermaster", "heating/boilermaster" );
		theMasterSwitch.sendDiscovery();
		theMasterSwitch.setState( false, true );
		theBoilerRoomMQ.flush();
		theBoilerRoomMQ.loop();
		theDemandMaster.setup( &theBoilerRoomMQ, "boilerdemand", "heating/boilerdemand", "heat" );
		theDemandMaster.sendDiscovery();
		theDemandMaster.setState( false, true );
		theBoilerRoomMQ.flush();
		theBoilerRoomMQ.loop();

		for( int i = 1; i <= MAX_ROOMS; i++ )
		{
			String lName = "boilerdemand" + String(i);
			theRoomSwitches[ i - 1 ].setup( &theBoilerRoomMQ, lName, "switch/" + lName );
			theRoomSwitches[ i - 1 ].sendDiscovery();
			theRoomSwitches[ i - 1 ].setState( false, true );
			theBoilerRoomMQ.flush();
			theBoilerRoomMQ.loop();
		}

		theAutoUpdate = new AutoUpdate( "boldlygo", lName.c_str() );
		theAutoUpdate->start();
	}
}

void loop()
{
	configManager.loop();
	if( configManager.isConfigured() )
	{
		theAutoUpdate->loop();
		theBoilerRoomMQ.loop();
		bool lState = false;
		for( int i = 0; i < MAX_ROOMS; i++ )
		{
			if( theRoomSwitches[i].getState() )
			{
				lState = true;
				break;
			}
		}
		theDemandMaster.setState( lState );
		theBoiler.setOn( theMasterSwitch.getState() );
		digitalWrite( BUILTIN_LED, !theMasterSwitch.getState() );
		theBoiler.loop();
	}
}
