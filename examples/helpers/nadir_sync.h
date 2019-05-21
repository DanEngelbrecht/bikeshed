#pragma once

struct NadirSync
{
    Bikeshed_ReadyCallback m_ReadyCallback;
    explicit NadirSync()
        : m_ReadyCallback { signal }
        , m_Lock(nadir::CreateLock(malloc(nadir::GetNonReentrantLockSize())))
        , m_ConditionVariable(nadir::CreateConditionVariable(malloc(nadir::GetConditionVariableSize()), m_Lock))
    {
    }
    ~NadirSync()
    {
        nadir::DeleteConditionVariable(m_ConditionVariable);
        free(m_ConditionVariable);
        nadir::DeleteNonReentrantLock(m_Lock);
        free(m_Lock);
    }

    void WakeAll()
    {
        nadir::LockNonReentrantLock(this->m_Lock);
        nadir::WakeAll(this->m_ConditionVariable);
        nadir::UnlockNonReentrantLock(this->m_Lock);
    }
    
    static void signal(Bikeshed_ReadyCallback* , uint8_t, uint32_t )
    {
    }
    nadir::HNonReentrantLock  m_Lock;
    nadir::HConditionVariable m_ConditionVariable;
};
