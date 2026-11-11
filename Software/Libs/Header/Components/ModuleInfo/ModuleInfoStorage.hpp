/**
 ******************************************************************************
 * @file    ModuleInfoStorage.hpp
 * @brief   Persistent per-module identification (name / revision / serial).
 *
 * @details
 *   Each physical board ("module") keeps three short strings: a name, a
 *   hardware revision and a serial number. The core module (the MCU board) has
 *   no EEPROM, so its record lives in an eMMC file; every other module stores
 *   its record at the start of the board's I2C EEPROM.
 *
 *   The on-storage layout is a versioned record (magic + version + fields), so
 *   an erased EEPROM (all 0xFF) or a missing file reads back as blank fields
 *   instead of garbage. Fields that were never programmed read as "" (never
 *   null). Writes are read-modify-write, so any subset of fields can be updated
 *   independently.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef COMPONENT_MODULE_INFO_STORAGE_HPP
#define COMPONENT_MODULE_INFO_STORAGE_HPP

#include <stdint.h>
#include <stddef.h>

namespace Interface { class II2c; }
namespace Hardware { class N24S64B; }

namespace Component {

class ModuleInfoStorage
{
public:
    /**
     * @brief Physical modules (boards) that carry an identification record.
     */
    enum class Module : uint8_t {
        CORE,   // MCU board (eMMC, has a barcode serial)
        VIEW,   // LCD board   (EEPROM on the main bus)
        LINK1,  // BLE board   (EEPROM on the sensor-1 bus)
        LINK2,  // GPS board   (EEPROM on the sensor-2 bus)
        BODY,   // body sensor (EEPROM on the body bus)
        COUNT
    };

    // Field capacity including the NUL terminator.
    static constexpr size_t skFieldSize = 32;

    /**
     * @brief Decoded module record returned to callers. Unset fields are "".
     */
    struct Info {
        char name[skFieldSize];
        char revision[skFieldSize];
        char serial[skFieldSize];
    };

    /**
     * @brief Construct with the storage backends injected.
     * @param viewBus     EEPROM bus for the VIEW module.
     * @param link1Bus    EEPROM bus for the LINK1 module.
     * @param link2Bus    EEPROM bus for the LINK2 module.
     * @param bodyBus     EEPROM bus for the BODY module.
     * @param coreFilePath Path of the CORE module record file (must outlive this).
     */
    ModuleInfoStorage(Interface::II2c& viewBus,
                      Interface::II2c& link1Bus,
                      Interface::II2c& link2Bus,
                      Interface::II2c& bodyBus,
                      const char*      coreFilePath);

    /**
     * @brief Longest string accepted by the setters (excludes the NUL).
     */
    static constexpr size_t maxFieldLen() { return skFieldSize - 1; }

    /**
     * @brief Read a module record. Unset fields come back as "".
     * @retval true on success (including a blank/unprogrammed record),
     *         false only on a storage/transport error.
     */
    bool read(Module m, Info& out);

    /**
     * @brief Update a single field, leaving the other two untouched.
     * @retval true on success, false on invalid input or storage error.
     */
    bool setName(Module m, const char* name);
    bool setRevision(Module m, const char* revision);
    bool setSerial(Module m, const char* serial);

private:
    static constexpr uint16_t skMagic   = 0x494D;  // 'I','M'
    static constexpr uint8_t  skVersion = 1;

    /**
     * @brief On-storage layout (shared by EEPROM and the core file).
     */
    struct Record {
        uint16_t magic;
        uint8_t  version;
        uint8_t  reserved;
        char     name[skFieldSize];
        char     revision[skFieldSize];
        char     serial[skFieldSize];
    };

    enum class Field { NAME, REVISION, SERIAL };

    bool setField(Module m, Field field, const char* value);

    bool readRecord(Module m, Record& rec);
    bool writeRecord(Module m, const Record& rec);

    static void initEmpty(Record& rec);
    static bool isValid(const Record& rec);
    static void recordToInfo(const Record& rec, Info& out);

    // Returns the EEPROM bus for a non-core module, or nullptr otherwise.
    Interface::II2c* eepromBus(Module m);

    // Page-aware EEPROM write that honours the 32-byte page boundary.
    static bool eepromWrite(Hardware::N24S64B& eeprom, const Record& rec);

    Interface::II2c& mViewBus;
    Interface::II2c& mLink1Bus;
    Interface::II2c& mLink2Bus;
    Interface::II2c& mBodyBus;
    const char*      mCoreFilePath;
};

} // namespace Component

#endif // COMPONENT_MODULE_INFO_STORAGE_HPP
