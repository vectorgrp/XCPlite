#pragma once
/* xcp.hpp - C++ wrapper for XCP server */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#include "main_cfg.h" 
#include "platform.h" 
#include "xcptl_cfg.h" // Transport Layer configuration
#include "xcp_cfg.h" // Protocoll Layer configuration

#ifdef OPTION_ENABLE_A2L_GEN
#include "A2L.hpp" // A2L generator
#endif

// XCP server 
// class Xcp is a polymorph singleton
class Xcp {

private:

	BOOL useTCP;
	BOOL usePTP;
	const uint8_t* addr;
	uint16_t port;

#ifdef OPTION_ENABLE_A2L_GEN
	A2L* a2lFile;
#endif	

protected:

	static MUTEX mutex;
	static Xcp* instance;

	Xcp();
	Xcp(const Xcp&) = delete; 
	~Xcp();

	static void lock() { ::mutexLock(&mutex); };
	static void unlock() { ::mutexUnlock(&mutex); };

public:

	class XcpEventDescriptor {
	public:
		const char* name; // name
		uint32_t cycleTime; // cycletime in ms
		uint8_t timeUnit; // timeCycle unit, 1us = 3, 10us = 4, 100us = 5, 1ms = 6, 10ms = 7, ...
		uint8_t timeCycle; // cycletime in units, 0 = sporadic or unknown
		uint8_t priority; // priority 0 = queued, 1 = pushing, 2 = realtime
		uint32_t size; // ext event size
		uint16_t sampleCount; // packed event sample count

		XcpEventDescriptor(const char* name0, uint32_t cycleTime0 = 0, uint8_t priority0 = 0, uint16_t sampleCount0 = 0, uint32_t size0 = 0) : 
			name(name0), cycleTime(cycleTime0), priority(priority0), size(size0), sampleCount(sampleCount0)
		{
			uint32_t c;

			// Convert cycle time to ASAM coding time cycle and time unit
			// RESOLUTION OF TIMESTAMP "UNIT_1US" = 3,"UNIT_10US" = 4,"UNIT_100US" = 5,"UNIT_1MS" = 6,"UNIT_10MS" = 7,"UNIT_100MS" = 8,
			c = cycleTime;
			timeUnit = 3;
			while (c >= 256) {
				c /= 10;
				timeUnit++;
			}
			timeCycle = (uint8_t)c;
		}
	};

	static Xcp* getInstance();

	virtual BOOL init(const uint8_t* addr, uint16_t port, BOOL useTCP, BOOL usePTP);
	virtual void shutdown();

	virtual BOOL onConnect();      // Callbacks
	virtual BOOL onPrepareDaq();
	virtual BOOL onStartDaq();
	virtual BOOL onStopDaq();

	virtual uint64_t getDaqClock(); // Get daq timestamp clock

	BOOL status();            // Status
	BOOL connected();
	BOOL daqRunning();

	void clearEventList(); // Event handling
	uint16_t createEvent(XcpEventDescriptor event);
	vector<XcpEventDescriptor>* getEventList();

	void event(uint16_t event);    // Event trigger
	void eventExt(uint16_t event, uint8_t* base);
	void eventAt(uint16_t event, uint64_t clock);
	void eventExtAt(uint16_t event, uint8_t* base, uint64_t clock);

	uint32_t getA2lAddr(uint8_t* p); // Get A2L addr from pointer	
	const char* getA2lFileName(); // Get A2L filename info for GET_ID name and upload 

	// Optional: A2L generation
#ifdef OPTION_ENABLE_A2L_GEN
	A2L* createA2L(const char* projectName); 
    A2L* getA2L() { return a2lFile; }
	void closeA2L();
#endif
};


// XCP object
// Allows to calibrate and measure instances and references to instances of classes
// Represents a A2L instance of a structure type
class XcpObject {

private:
	uint16_t instanceId;
	const char* className;
	int classSize;

protected:
	const char* instanceName;

	// Create components (A2L STRUCTURE_COMPONENTS) components of inheriting classes
	virtual void a2lCreateTypedefComponents(A2L* a2l) { (void)a2l; };

public:

	// Create an A2L INSTANCE (instanceName) for the class with className/classSize
	XcpObject(const char* instanceName, const char* className, int classSize);

	// Create the typedef (A2L TYPEDEF_STRUCTURE) for this class, calls a2lCreateTypedefComponents to add components 
	void a2lCreateTypedef();

	// Trigger the XCP event (instanceId) associated with this A2L INSTANCE
	void xcpEvent();
	void xcpEvent(uint8_t* base);


};


#define XcpDynObject(instanceName,className) XcpObject(instanceName,#className,sizeof(className))




