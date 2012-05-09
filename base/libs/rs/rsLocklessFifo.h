/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_RS_LOCKLESS_FIFO_H
#define ANDROID_RS_LOCKLESS_FIFO_H


#include "rsUtils.h"

namespace android {


// A simple FIFO to be used as a producer / consumer between two
// threads.  One is writer and one is reader.  The common cases
// will not require locking.  It is not threadsafe for multiple
// readers or writers by design.

class LocklessCommandFifo
{
public:
    bool init(uint32_t size);
    void shutdown();

    LocklessCommandFifo();
    ~LocklessCommandFifo();


protected:
    class Signal {
    public:
        Signal();
        ~Signal();

        bool init();

        void set();
        void wait();

    protected:
        bool mSet;
        pthread_mutex_t mMutex;
        pthread_cond_t mCondition;
    };

    uint8_t * volatile mPut;
    uint8_t * volatile mGet;
    uint8_t * mBuffer;
    uint8_t * mEnd;
    uint8_t mSize;
    bool mInShutdown;

    Signal mSignalToWorker;
    Signal mSignalToControl;



public:
    void * reserve(uint32_t bytes);
    void commit(uint32_t command, uint32_t bytes);
    void commitSync(uint32_t command, uint32_t bytes);

    void flush();
    const void * get(uint32_t *command, uint32_t *bytesData);
    void next();

    void makeSpace(uint32_t bytes);

    bool isEmpty() const;
    uint32_t getFreeSpace() const;


private:
    void dumpState(const char *) const;
};


}
#endif
