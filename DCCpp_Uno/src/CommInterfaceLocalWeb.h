#pragma once

/*
 * CommInterfaceESP.h
 *
 *  Created on: Mar 3, 2017
 *      Author: mdunston
 */

#include "Config.h"

#if COMM_INTERFACE == 5
#include "CommInterface.h"
class LocalWebInterface : public CommInterface {
public:
	LocalWebInterface();
	virtual void process() override;
	virtual void showConfiguration() override;
	virtual void showInitInfo() override;
	virtual void send(const char *buf);
};

#endif
