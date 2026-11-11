/**
 ******************************************************************************
 * @file    ProdTestFileSystem.hpp
 * @brief   Production test for the File System.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ProdTest/IProdTest.hpp"

namespace ProdTest {

class FileSystem : public Interface::IProdTest {
public:
    FileSystem(const std::string& id);
    ~FileSystem() override = default;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;
};

}   // namespace ProdTest
