#include <gtest/gtest.h>
#include <iostream>

#include "shuaikv/utils/lock.hpp"

TEST(Lock, Function) {
    shuaikv::common::RWLock lock;
    {
        shuaikv::common::RWLock::ReadLock r_lock_1(lock);
        shuaikv::common::RWLock::ReadLock r_lock_2(lock);
    }

    {
        // shuaikv::common::RWLock::ReadLock r_lock_1(lock);
        shuaikv::common::RWLock::WriteLock w_lock(lock);
    }
}