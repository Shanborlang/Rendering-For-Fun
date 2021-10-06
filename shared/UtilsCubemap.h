#pragma once

#include "Bitmap.h"

Bitmap ConvertEquirectangularMapToVerticalCross(const Bitmap& b);
Bitmap ConvertVerticalCrossToCubeMapFaces(const Bitmap& b);