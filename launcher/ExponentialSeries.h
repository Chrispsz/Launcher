#pragma once

#include <algorithm>

class ExponentialSeries
{
public:
    ExponentialSeries(unsigned minVal, unsigned maxVal)
        : m_min(minVal), m_max(maxVal), m_current(minVal)
    {}

    unsigned operator()()
    {
        unsigned result = m_current;
        if (m_current < m_max)
        {
            m_current = std::min(m_current * 2, m_max);
        }
        return result;
    }

    void reset()
    {
        m_current = m_min;
    }

private:
    unsigned m_min;
    unsigned m_max;
    unsigned m_current;
};
