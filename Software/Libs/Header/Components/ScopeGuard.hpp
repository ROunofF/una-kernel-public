#ifndef __SCOPE_GUARD_HPP
#define __SCOPE_GUARD_HPP

template<typename F>
class ScopeGuard
{
public:

    explicit ScopeGuard(F f)
        : mFunc(f)
        , mActive(true)
    {}

    ~ScopeGuard() {
        if (mActive) {
            mFunc();
        }
    }

    void dismiss()
    {
        mActive = false;
    }

private:

    F mFunc;
    bool mActive;
};

// helper-function
template<typename F>
ScopeGuard<F> MakeScopeGuard(F f)
{
    return ScopeGuard<F>(f);
}

#endif
