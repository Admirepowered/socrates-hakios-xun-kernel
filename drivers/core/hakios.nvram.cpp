/*
 * HakiosNVRAM: the NVRAM controller this port was missing.
 *
 * The loader hands the kernel a whole, well-formed NVRAM image in the device tree ("nvram-proxy-data"
 * under /chosen/memory-map), so reads work from the first boot: IODTNVRAM parses that image and every
 * variable is there.  Writing does not: IONVRAMCHRPHandler serialises the variables only through an
 * IONVRAMController, and this port had none, so every sync ended in
 *
 *     IONVRAMCHRPHandler.cpp:serializeVariables:615 - No _nvramController
 *     IONVRAMCHRPHandler.cpp:sync:894 - serializeVariables failed, ret=0xe00002d8
 *
 * — kIOReturnNotReady, every shutdown.  What that costs is anything the system wants to remember: the
 * boot-args it sets, the panic counter, whether the phone was shut down cleanly.
 *
 * This is that controller, in memory: the banks live in a buffer of ours for as long as the machine is
 * up.  It is not persistent -- nothing writes back to the phone's NVRAM partition -- but it is a real
 * controller, so the serialise path completes and the system sees its variables stored.
 */

#include <IOKit/IOLib.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/nvram/IONVRAMController.h>

#define HAKIOS_NVRAM_BANKS   2
#define HAKIOS_NVRAM_BANK    (64 * 1024)

class HakiosNVRAM : public IONVRAMController
{
	OSDeclareDefaultStructors(HakiosNVRAM);

public:
	bool     start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void     stop(IOService *provider) APPLE_KEXT_OVERRIDE;

	IOReturn read(IOByteCount offset, UInt8 *buffer, IOByteCount length) APPLE_KEXT_OVERRIDE;
	IOReturn write(IOByteCount offset, UInt8 *buffer, IOByteCount length) APPLE_KEXT_OVERRIDE;
	void     sync(void) APPLE_KEXT_OVERRIDE;
	IOReturn select(uint32_t bank) APPLE_KEXT_OVERRIDE;
	IOReturn eraseBank(void) APPLE_KEXT_OVERRIDE;

private:
	uint8_t *banks;
	uint32_t current;
};

OSDefineMetaClassAndStructors(HakiosNVRAM, IONVRAMController);

bool
HakiosNVRAM::start(IOService *provider)
{
	if (!IONVRAMController::start(provider)) {
		return false;
	}
	banks = (uint8_t *)IOMallocZero(HAKIOS_NVRAM_BANKS * HAKIOS_NVRAM_BANK);
	if (banks == NULL) {
		return false;
	}
	memset(banks, 0xFF, HAKIOS_NVRAM_BANKS * HAKIOS_NVRAM_BANK);
	current = 0;
	/*
	 * registerService() is what hands the controller to the platform expert -- IONVRAMController overrides it
	 * to call getPlatform()->registerNVRAMController(this) -- and the platform expert passes it to the NVRAM
	 * format it chose, IONVRAMCHRPHandler on this device tree.  Without it the handler keeps its null
	 * controller and every sync answers kIOReturnNotReady.
	 */
	registerService();
	IOLog("hakios-nvram: a controller in memory, %d banks of %d bytes\n", HAKIOS_NVRAM_BANKS,
	    HAKIOS_NVRAM_BANK);
	return true;
}

void
HakiosNVRAM::stop(IOService *provider)
{
	if (banks != NULL) {
		IOFree(banks, HAKIOS_NVRAM_BANKS * HAKIOS_NVRAM_BANK);
		banks = NULL;
	}
	IONVRAMController::stop(provider);
}

IOReturn
HakiosNVRAM::read(IOByteCount offset, UInt8 *buffer, IOByteCount length)
{
	if (banks == NULL || current >= HAKIOS_NVRAM_BANKS || offset > HAKIOS_NVRAM_BANK ||
	    length > HAKIOS_NVRAM_BANK - offset) {
		return kIOReturnBadArgument;
	}
	memcpy(buffer, banks + current * HAKIOS_NVRAM_BANK + offset, (size_t)length);
	return kIOReturnSuccess;
}

IOReturn
HakiosNVRAM::write(IOByteCount offset, UInt8 *buffer, IOByteCount length)
{
	if (banks == NULL || current >= HAKIOS_NVRAM_BANKS || offset > HAKIOS_NVRAM_BANK ||
	    length > HAKIOS_NVRAM_BANK - offset) {
		return kIOReturnBadArgument;
	}
	memcpy(banks + current * HAKIOS_NVRAM_BANK + offset, buffer, (size_t)length);
	return kIOReturnSuccess;
}

void
HakiosNVRAM::sync(void)
{
	/* in memory: there is nothing to flush to, and nothing to wait for */
}

IOReturn
HakiosNVRAM::select(uint32_t bank)
{
	if (bank >= HAKIOS_NVRAM_BANKS) {
		return kIOReturnBadArgument;
	}
	current = bank;
	return kIOReturnSuccess;
}

IOReturn
HakiosNVRAM::eraseBank(void)
{
	if (banks == NULL || current >= HAKIOS_NVRAM_BANKS) {
		return kIOReturnNotReady;
	}
	memset(banks + current * HAKIOS_NVRAM_BANK, 0xFF, HAKIOS_NVRAM_BANK);
	return kIOReturnSuccess;
}

/*
 * Started from the port's own init, the way its other services are: there is no device tree node for a
 * controller this small.  The platform expert is the provider, so the service has a place in the tree.
 */
extern "C" void
HakiosNVRAMStart(void)
{
	IOService *provider = IOService::getPlatform();
	HakiosNVRAM *controller;

	if (provider == NULL) {
		return;
	}
	controller = new HakiosNVRAM;
	if (controller == NULL) {
		return;
	}
	if (!controller->init()) {
		controller->release();
		return;
	}
	if (!controller->attach(provider)) {
		controller->release();
		return;
	}
	if (!controller->start(provider)) {
		controller->detach(provider);
		controller->release();
	}
}
