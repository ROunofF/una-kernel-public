/**
 ******************************************************************************
 * @file    IBuzzer.hpp
 * @brief   Buzzer interface.
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __IBUZZER_HPP
#define __IBUZZER_HPP

#include <cstdint>

namespace Interface
{

/**
 * @brief   Buzzer interface.
 */
class IBuzzer {
public:

    // Maximum Notes includes pauses.
    static const uint8_t skMaxNotes = 10;


    struct Note {
        uint32_t time = 100;    ///< Duration in ms
        uint8_t  level = 2;     ///< Sound level 1,2,3, 0 - no sound
        uint16_t frequency = 0; ///< Pitch in Hz, 0 - the buzzer's default
    };

    /**
     * @brief   Play short beep.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool play() = 0;

    /**
     * @brief   Play note.
     * @note    Commands are queued, so you can call this method without delay.
     * @param   note: Note_t to play;
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool play(Note note) = 0;

    /**
     * @brief   Play melody.
     * @param   melody: array of notes.
     * @param   len: length of the array. (Max len is skNoteQueueSize)
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool play(const Note melody[], uint8_t len) = 0;

    /**
     * @brief   Check whether the buzzer is playing.
     * @retval  'true' if playing, 'false' otherwise.
     */
    virtual bool isPlaying() = 0;

    /**
     * @brief   Stop playing melody.
     */
    virtual void stop() = 0;

protected:

    /**
     * @brief   Destructor.
     */
    virtual ~IBuzzer() = default;

};

} /* namespace Interface */

#endif /* __IBUZZER_HPP */
