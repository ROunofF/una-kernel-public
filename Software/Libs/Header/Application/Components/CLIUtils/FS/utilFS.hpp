#ifndef __utilFileSystem_h__
#define __utilFileSystem_h__

#include "Porting.h"
#include "CLI/UtilBase.hpp"
#include "CLI/ICommand.h"

#include "ff.h"

namespace Components {

namespace CliUtils {

class UtilFS : public UtilBase
{
public:
    static UtilFS& getInstance();

    UtilFS(UtilFS const&)      = delete;
    void operator=(UtilFS const&) = delete;

    UtilFS(UtilFS&&)            = delete;
    UtilFS& operator=(UtilFS&&) = delete;

    ICLICommand_t *getCommands() override;

private:
    ICLICommand_t mCommands;

    explicit UtilFS();
    ~UtilFS() override = default;

    static void checkCommand(ICLICommand_t* cmd, CLIInput_t* input);
    static void prompt(void);

    static const char* getName(ICLICommand_t* cmd);
    static const char* getDesc(ICLICommand_t* cmd);
    static const char* getHelp(ICLICommand_t* cmd);
    static void        execute(ICLICommand_t* cmd,
                               CLIInput_t*    input,
                               int            argc,
                               const char**   argv);
};

}
}

#endif
