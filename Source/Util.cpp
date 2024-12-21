#include <Util.h>

// StopWatch
// -----------------------------------------

StopWatch::StopWatch() { mPrevTime = std::chrono::steady_clock::now(); }

void StopWatch::Read(float& value)
{
    auto now  = std::chrono::steady_clock::now();
    value     = std::chrono::duration_cast<std::chrono::microseconds>(now - mPrevTime).count() / 1000000.0f;
    mPrevTime = now;
}

// ScrollingBuffer
// ------------------------------------------

ScrollingBuffer::ScrollingBuffer(int maxSize)
{
    this->mSizeMax = maxSize;
    mOffset        = 0;
    mData.reserve(maxSize);
}

void ScrollingBuffer::AddPoint(float x, float y)
{
    if (mData.size() < mSizeMax)
        mData.push_back(ImVec2(x, y));
    else
    {
        mData[mOffset] = ImVec2(x, y);

        mOffset = (mOffset + 1) % mSizeMax;
    }
}

void ScrollingBuffer::Erase()
{
    if (mData.size() > 0)
    {
        mData.shrink(0);
        mOffset = 0;
    }
}

// MovingAverage
// --------------------------------------------

MovingAverage::MovingAverage(int windowSize) : mWindowSize(windowSize), mCount(0), mSum(0.0f), mIndex(0) { mValues = new float[mWindowSize]; }

// Destructor to free allocated memory
MovingAverage::~MovingAverage() { delete[] mValues; }

// Add a new value to the moving average
void MovingAverage::AddValue(float value)
{
    if (mCount == mWindowSize)
        mSum -= mValues[mIndex];
    else
        ++mCount;

    mValues[mIndex] = value;
    mSum += value;

    mIndex = (mIndex + 1) % mWindowSize;
}

float MovingAverage::GetAverage() const
{
    if (mCount == 0)
        return 0.0f;

    return mSum / mCount;
}
