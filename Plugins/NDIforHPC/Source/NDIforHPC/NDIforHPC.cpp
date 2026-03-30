#include "Modules/ModuleManager.h"
#include "StreamingSourceAttacher.h"

class FNDIforHPCModule : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FNDIforHPCModule, NDIforHPC);
