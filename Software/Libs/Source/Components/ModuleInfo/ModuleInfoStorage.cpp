/**
 ******************************************************************************
 * @file    ModuleInfoStorage.cpp
 * @brief   Persistent per-module identification (name / revision / serial).
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Components/ModuleInfo/ModuleInfoStorage.hpp"

#include "II2c.hpp"
#include "N24S64B.hpp"

#include "File.hpp"

#include <string.h>

namespace Component {

ModuleInfoStorage::ModuleInfoStorage(Interface::II2c& viewBus,
                                     Interface::II2c& link1Bus,
                                     Interface::II2c& link2Bus,
                                     Interface::II2c& bodyBus,
                                     const char*      coreFilePath) :
    mViewBus(viewBus),
    mLink1Bus(link1Bus),
    mLink2Bus(link2Bus),
    mBodyBus(bodyBus),
    mCoreFilePath(coreFilePath)
{}

bool ModuleInfoStorage::read(Module m, Info& out)
{
    Record rec{};

    if (!readRecord(m, rec)) {
        return false;
    }

    recordToInfo(rec, out);

    return true;
}

bool ModuleInfoStorage::setName(Module m, const char* name)
{
    return setField(m, Field::NAME, name);
}

bool ModuleInfoStorage::setRevision(Module m, const char* revision)
{
    return setField(m, Field::REVISION, revision);
}

bool ModuleInfoStorage::setSerial(Module m, const char* serial)
{
    return setField(m, Field::SERIAL, serial);
}

bool ModuleInfoStorage::setField(Module m, Field field, const char* value)
{
    if (value == nullptr || strlen(value) > maxFieldLen()) {
        return false;
    }

    // Read-modify-write: keep the other two fields intact. A blank/invalid
    // record is initialized to empty so a first-time write still succeeds.
    Record rec{};
    if (!readRecord(m, rec)) {
        return false;
    }

    char* dst = nullptr;
    switch (field) {
        case Field::NAME:     dst = rec.name;     break;
        case Field::REVISION: dst = rec.revision; break;
        case Field::SERIAL:   dst = rec.serial;   break;
    }

    memset(dst, 0, skFieldSize);
    memcpy(dst, value, strlen(value));

    rec.magic   = skMagic;
    rec.version = skVersion;

    return writeRecord(m, rec);
}

void ModuleInfoStorage::initEmpty(Record& rec)
{
    memset(&rec, 0, sizeof(Record));
    rec.magic   = skMagic;
    rec.version = skVersion;
}

bool ModuleInfoStorage::isValid(const Record& rec)
{
    return rec.magic == skMagic && rec.version == skVersion;
}

void ModuleInfoStorage::recordToInfo(const Record& rec, Info& out)
{
    memset(&out, 0, sizeof(Info));

    if (!isValid(rec)) {
        return;
    }

    // Defensive: ensure NUL termination regardless of stored content.
    memcpy(out.name, rec.name, skFieldSize);
    memcpy(out.revision, rec.revision, skFieldSize);
    memcpy(out.serial, rec.serial, skFieldSize);

    out.name[skFieldSize - 1]     = '\0';
    out.revision[skFieldSize - 1] = '\0';
    out.serial[skFieldSize - 1]   = '\0';
}

bool ModuleInfoStorage::readRecord(Module m, Record& rec)
{
    initEmpty(rec);

    if (m == Module::CORE) {
        FS::File file(mCoreFilePath);
        if (!file.open()) {
            // No record programmed yet - report a blank record, not an error.
            return true;
        }

        Record stored{};
        size_t br = 0;
        bool ok = file.read(reinterpret_cast<char*>(&stored), sizeof(Record), br);
        file.close();

        if (!ok) {
            return false;
        }

        if (br == sizeof(Record) && isValid(stored)) {
            rec = stored;
        }

        return true;
    }

    Interface::II2c* bus = eepromBus(m);
    if (bus == nullptr) {
        return false;
    }

    Hardware::N24S64B eeprom(*bus);
    if (!eeprom.isReady()) {
        return false;
    }

    Record stored{};
    if (!eeprom.readBytes(0, reinterpret_cast<uint8_t*>(&stored),
                          static_cast<uint16_t>(sizeof(Record)))) {
        return false;
    }

    if (isValid(stored)) {
        rec = stored;
    }

    return true;
}

bool ModuleInfoStorage::writeRecord(Module m, const Record& rec)
{
    if (m == Module::CORE) {
        FS::File file(mCoreFilePath);
        if (!file.open(true, true)) {
            return false;
        }

        size_t bw = 0;
        bool ok = file.write(reinterpret_cast<const char*>(&rec), sizeof(Record), bw);
        file.close();

        return ok && bw == sizeof(Record);
    }

    Interface::II2c* bus = eepromBus(m);
    if (bus == nullptr) {
        return false;
    }

    Hardware::N24S64B eeprom(*bus);
    if (!eeprom.isReady()) {
        return false;
    }

    return eepromWrite(eeprom, rec);
}

bool ModuleInfoStorage::eepromWrite(Hardware::N24S64B& eeprom, const Record& rec)
{
    // writeBytes() would wrap inside a single 32-byte page, so split the record
    // into page-aligned chunks and use writePage() for each.
    //
    // The write is intentionally NOT atomic: a failure partway through can leave
    // a record with a valid header (written first) but partially updated fields.
    // We deliberately keep this simple - this is factory/production tooling where
    // the operator programs and then reads the value back to confirm it, so a
    // rollback/journaling scheme is not worth the added complexity here.
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&rec);
    uint16_t addr = 0;
    uint16_t left = static_cast<uint16_t>(sizeof(Record));

    while (left > 0) {
        uint16_t pageOffset = addr % Hardware::N24S64B::skPageSize;
        uint16_t chunk      = Hardware::N24S64B::skPageSize - pageOffset;
        if (chunk > left) {
            chunk = left;
        }

        if (!eeprom.writePage(addr, p, static_cast<uint8_t>(chunk))) {
            return false;
        }

        addr += chunk;
        p    += chunk;
        left -= chunk;
    }

    return true;
}

Interface::II2c* ModuleInfoStorage::eepromBus(Module m)
{
    switch (m) {
        case Module::VIEW:  return &mViewBus;
        case Module::LINK1: return &mLink1Bus;
        case Module::LINK2: return &mLink2Bus;
        case Module::BODY:  return &mBodyBus;
        default:            return nullptr;
    }
}

} // namespace Component
