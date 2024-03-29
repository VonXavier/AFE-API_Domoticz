#include "stdafx.h"
#include "../hardware/AFE-Firmware/AFE-Firmware.h"
#include "../main/Helper.h"
#include "../main/Logger.h"
#include "../main/SQLHelper.h"
#include "../main/mainworker.h"
#include "../main/WebServer.h"
#include "../webserver/cWebem.h"
#include "../json/json.h"
#include "hardwaretypes.h"
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <sstream>

CAfe-Firmware::CAfe-Firmware(const int ID)
{
	m_HwdID=ID;
	m_bSkipReceiveCheck = true;
}

CAfe-Firmware::~CAfe-Firmware(void)
{
	m_bIsStarted=false;
}

void CAfe-Firmware::Init()
{
}

bool CAfe-Firmware::StartHardware()
{
	RequestStart();

	Init();
	m_bIsStarted=true;
	sOnConnected(this);
	return true;
}

bool CAfe-Firmware::StopHardware()
{
	m_bIsStarted=false;
    return true;
}

bool CAfe-Firmware::WriteToHardware(const char *pdata, const unsigned char length)
{
#ifdef _DEBUG
	if (length < 2)
		return false;
	std::string sdevicetype = RFX_Type_Desc(pdata[1], 1);
	if (pdata[1] == pTypeGeneral)
	{
		const _tGeneralDevice *pMeter = reinterpret_cast<const _tGeneralDevice*>(pdata);
		sdevicetype += "/" + std::string(RFX_Type_SubType_Desc(pMeter->type, pMeter->subtype));
	}
	Log(LOG_STATUS, "Received null operation for %s", sdevicetype.c_str());
#endif
	return true;
}

//Webserver helpers
namespace http {
	namespace server {

		struct _mappedsensorname {
			const int mappedvalue;
			const int type;
			const int subtype;
		};

		static const _mappedsensorname mappedsensorname[] =
		{
			{ 249, 0xF9, 0x01 }, //Air Quality
			{ 11,  0xF3, 0x1A }, //Barometer
			{ 113, 0x71, 0x00 }, //Counter
			{ 14,  0xF3, 0x1C }, //Counter Incremental
			{ 1004,0xF3, 0x1F }, //Czujnik PM10 i PM2.5
			{ 1004,0xF3, 0x1F }, //Custom Sensor
			{ 3,   0xFB, 0x02 }, //Gas
			{ 81,  0x51, 0x01 }, //Humidity
			{ 246, 0xF6, 0x01 }, //Lux
			{ 1,   0xF3, 0x09 }, //Pressure (Bar)
			{ 241, 0xF1, 0x02 }, //RGB Switch
			{ 1003,0xF1, 0x01 }, //RGBW Switch
			{ 6,   0xF4, 0x49 }, //Switch
			{ 80,  0x50, 0x05 }, //Temperature
			{ 82,  0x52, 0x01 }, //Temp+Hum
			{ 84,  0x54, 0x01 }, //Temp+Hum+Baro
			{ 247, 0xF7, 0x01 }, //Temp+Baro
			{ 5,   0xF3, 0x13 }, //Text
			{ 8,   0xF2, 0x01 }, //Thermostat Setpoint
			{ 12,  0xF3, 0x01 }, //Visibility
			{ 4,   0xF3, 0x08 }, //Voltage
			{ 86,  0x56, 0x01 }, //Wind
			{ 1001,0x56, 0x04 } //Wind+Temp+Chill
		};

		//TODO: Is this function called from anywhere, or can it be removed?
		void CWebServer::RType_CreateMappedSensor(WebEmSession & session, const request& req, Json::Value &root)
		{ // deprecated (for dzVents). Use RType_CreateDevice
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			std::string ssensorname = request::findValue(&req, "sensorname");
			std::string ssensortype = request::findValue(&req, "sensortype");
			std::string soptions = request::findValue(&req, "sensoroptions");

			if ((idx == "") || (ssensortype.empty()) || (ssensorname.empty()))
				return;

			int sensortype = atoi(ssensortype.c_str());
			unsigned int type = 0;
			unsigned int subType = 0;
			uint64_t DeviceRowIdx = (uint64_t )-1;

			for (int i = 0; i < sizeof(mappedsensorname) / sizeof(mappedsensorname[0]); i++)
			{
				if (mappedsensorname[i].mappedvalue == sensortype)
				{
					type = mappedsensorname[i].type;
					subType = mappedsensorname[i].subtype;

					int HwdID = atoi(idx.c_str());

					//Make a unique number for ID
					std::vector<std::vector<std::string> > result;
					result = m_sql.safe_query("SELECT MAX(ID) FROM DeviceStatus");

					unsigned long nid = 1; //could be the first device ever

					if (!result.empty())
					{
						nid = atol(result[0][0].c_str()) + 1;
					}
					unsigned long vs_idx = nid; // OTO keep idx to be returned before masking
					nid += 82000;

					bool bPrevAcceptNewHardware = m_sql.m_bAcceptNewHardware;
					m_sql.m_bAcceptNewHardware = true;

					DeviceRowIdx = m_sql.CreateDevice(HwdID, type, subType, ssensorname, nid, soptions);

					m_sql.m_bAcceptNewHardware = bPrevAcceptNewHardware;

					if (DeviceRowIdx != -1)
					{
						root["status"] = "OK";
						root["title"] = "Nowy czujnik";
						root["idx"] = std::to_string(vs_idx);
					}
					break;
				}
			}
		}

		void CWebServer::RType_CreateDevice(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			std::string ssensorname = request::findValue(&req, "sensorname");
			std::string soptions = request::findValue(&req, "sensoroptions");

			std::string ssensormappedtype = request::findValue(&req, "sensormappedtype");

			std::string devicetype = request::findValue(&req, "devicetype");
			std::string devicesubtype = request::findValue(&req, "devicesubtype");

			if ((idx == "") || (ssensorname.empty()))
				return;

			unsigned int type;
			unsigned int subType;

			if (!ssensormappedtype.empty())
			{ // for creating Afe-Firmware from web (for example ssensormappedtype=0xF401)
				uint16_t fullType;
				std::stringstream ss;
				ss << std::hex << ssensormappedtype;
				ss >> fullType;

				type = fullType >> 8;
				subType = fullType & 0xFF;
			}
			else
				if (!devicetype.empty() && !devicesubtype.empty())
				{ // for creating any device (type=x&subtype=y) from json api or code
					type = atoi(devicetype.c_str());
					subType = atoi(devicesubtype.c_str());
				}
				else
					return;

			int HwdID = atoi(idx.c_str());

			//Make a unique number for ID
			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT MAX(ID) FROM DeviceStatus");

			unsigned long nid = 1; //could be the first device ever

			if (!result.empty())
			{
				nid = atol(result[0][0].c_str()) + 1;
			}
			unsigned long vs_idx = nid; // OTO keep idx to be returned before masking
			nid += 82000;

			bool bPrevAcceptNewHardware = m_sql.m_bAcceptNewHardware;
			m_sql.m_bAcceptNewHardware = true;

			uint64_t DeviceRowIdx = m_sql.CreateDevice(HwdID, type, subType, ssensorname, nid, soptions);

			m_sql.m_bAcceptNewHardware = bPrevAcceptNewHardware;

			if (DeviceRowIdx != -1)
			{
				root["status"] = "OK";
				root["title"] = "CreateSensor";
				root["idx"] = std::to_string(vs_idx);
			}
		}

	}
}

