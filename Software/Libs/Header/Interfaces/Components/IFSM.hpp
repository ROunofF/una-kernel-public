/**
 *******************************************************************************
 * @file    IFSM.hpp
 * @brief   Base class for a finite state machine.
 *
 *******************************************************************************
 *
 *******************************************************************************
 */

#pragma once

#include "Porting.h"
#include "SwTimer.hpp"

#include <vector>

namespace Interface {

template <typename T>
class IFSMFlag {
public:
    IFSMFlag() : mStatus(T{})
    {}

    virtual ~IFSMFlag() = default;

    bool operator()(T newStatus)
    {
        if (mStatus == newStatus) {
            return false;
        }

        mStatus = newStatus;

        return true;
    }

    T operator()() const
    {
        return mStatus;
    }

    void set(T status)
    {
        mStatus = status;
    }

    void clear()
    {
        mStatus = T{};
    }

    T status()
    {
        return mStatus;
    }

private:
    T mStatus;
};

template <typename StateT, typename EventT, typename FlagT = bool>
class IFSM {
private:
    virtual void processing(EventT e) = 0;
    virtual void processingNewState() = 0;

    std::vector<::Driver::SwTimer> mTimers;
    std::vector<IFSMFlag<FlagT>>   mFlags;

    StateT mState;

protected:
    class FlagsView {
    public:
        explicit FlagsView(std::vector<IFSMFlag<FlagT>>& v) : mVector(v) {}
        IFSMFlag<FlagT>& operator[](size_t i)                            { return mVector.at(i); }
        const IFSMFlag<FlagT>& operator[](size_t i) const                { return mVector.at(i); }
    private:
        std::vector<IFSMFlag<FlagT>>& mVector;
    };

    class TimersView {
    public:
        explicit TimersView(std::vector<::Driver::SwTimer>& v) : mVector(v) {}
        ::Driver::SwTimer& operator[](size_t i)                             { return mVector.at(i); }
        const ::Driver::SwTimer& operator[](size_t i) const                 { return mVector.at(i); }
    private:
        std::vector<::Driver::SwTimer>& mVector;
    };

    FlagsView  Flag;
    TimersView Timer;

    void setState(StateT state)
    {
        mState = state;
    }

public:
    IFSM(uint8_t tmrCount = 0, uint8_t flgCount = 0)
        : mTimers()
        , mFlags()
        , mState{}
        , Flag(mFlags)
        , Timer(mTimers)
    {
        mTimers.resize(tmrCount);
        mFlags.resize(flgCount);
    }

    virtual ~IFSM() = default;

    void reset()
    {
        mState = StateT{};
    }

    bool refresh(EventT e = EventT{})
    {
        StateT oldState = mState;

        processing(e);

        if (oldState == mState) {
            return false;
        }

        processingNewState();

        return true;
    }
    
    StateT getState()
    {
        return mState;
    }
};

}
