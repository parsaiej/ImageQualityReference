#ifndef COMMON_H
#define COMMON_H

namespace ImageQualityReference
{

#define NRI_ABORT_ON_FAILURE(result)      \
    if ((result) != nri::Result::SUCCESS) \
        exit(1);

#define NRI_ABORT_ON_FALSE(result) \
    if (!(result))                 \
        exit(1);

} // namespace ImageQualityReference

#endif
