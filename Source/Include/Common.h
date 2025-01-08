#ifndef COMMON_H
#define COMMON_H

namespace ImageQualityReference
{
    struct Vector2iCmp
    {
        bool operator()(const Eigen::Vector2i& lhs, const Eigen::Vector2i& rhs) const
        {
            if (lhs.x() != rhs.x())
                return lhs.x() < rhs.x();
            return lhs.y() < rhs.y();
        }
    };

#define NRI_ABORT_ON_FAILURE(result)      \
    if ((result) != nri::Result::SUCCESS) \
        exit(1);

#define NRI_ABORT_ON_FALSE(result) \
    if (!(result))                 \
        exit(1);

} // namespace ImageQualityReference

#endif
