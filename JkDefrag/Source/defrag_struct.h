#pragma once

#include <tchar.h>

/* The colors used by the defragger. */
//#define COLOREMPTY        0     /* Empty diskspace. */
//#define COLORALLOCATED    1     /* Used diskspace / system files. */
//#define COLORUNFRAGMENTED 2     /* Unfragmented files. */
//#define COLORUNMOVABLE    3     /* Unmovable files. */
//#define COLORFRAGMENTED   4     /* Fragmented files. */
//#define COLORBUSY         5     /* Busy color. */
//#define COLORMFT          6     /* MFT reserved zones. */
//#define COLORSPACEHOG     7     /* Spacehogs. */
//#define COLORBACK         8     /* Background color. */

class DefragStruct {
public:
    DefragStruct();
    ~DefragStruct();

    wchar_t versiontext_[100];

    enum colors {
        COLOREMPTY,
        COLORALLOCATED,
        COLORUNFRAGMENTED,
        COLORUNMOVABLE,
        COLORFRAGMENTED,
        COLORBUSY,
        COLORMFT,
        COLORSPACEHOG,
        COLORBACK
    };

protected:

private:
};
