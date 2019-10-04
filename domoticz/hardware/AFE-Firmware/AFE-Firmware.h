#pragma once

#include "DomoticzHardware.h"

class CAfe-Firmware : public CDomoticzHardwareBase
{
public:
	explicit CAfe-Firmware(const int ID);
	~CAfe-Firmware(void);
	bool WriteToHardware(const char *pdata, const unsigned char length) override;
private:
	void Init();
	bool StartHardware() override;
	bool StopHardware() override;
};

