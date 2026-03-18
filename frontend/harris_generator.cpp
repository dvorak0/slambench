// Harris Generator - AOT compiled Harris using Halide
// Based on lesson_21 approach

#include "Halide.h"
#include <stdint.h>

using namespace Halide;

class Harris : public Generator<Harris> {
public:
    Input<Buffer<uint8_t, 2>> input{"input", {752, 480}};  // Default size
    Output<Buffer<float, 2>> output{"output", {752, 480}};
    
    void generate() {
        Var x("x"), y("y");
        
        // Boundary conditions
        Func in_f("in_f");
        in_f(x, y) = BoundaryConditions::repeat_edge(input)(x, y);
        
        // Grayscale (already grayscale input)
        Func gray("gray");
        gray(x, y) = in_f(x, y);
        
        // Sobel gradients with 1/12 scale
        Func Iy("Iy");
        Iy(x, y) = gray(x - 1, y - 1) * (-1.0f / 12.0f) + gray(x - 1, y + 1) * (1.0f / 12.0f) +
                   gray(x, y - 1) * (-2.0f / 12.0f) + gray(x, y + 1) * (2.0f / 12.0f) +
                   gray(x + 1, y - 1) * (-1.0f / 12.0f) + gray(x + 1, y + 1) * (1.0f / 12.0f);

        Func Ix("Ix");
        Ix(x, y) = gray(x - 1, y - 1) * (-1.0f / 12.0f) + gray(x + 1, y - 1) * (1.0f / 12.0f) +
                   gray(x - 1, y) * (-2.0f / 12.0f) + gray(x + 1, y) * (2.0f / 12.0f) +
                   gray(x - 1, y + 1) * (-1.0f / 12.0f) + gray(x + 1, y + 1) * (1.0f / 12.0f);
        
        // Covariance
        Func Ixx("Ixx"), Iyy("Iyy"), Ixy("Ixy");
        Ixx(x, y) = Ix(x, y) * Ix(x, y);
        Iyy(x, y) = Iy(x, y) * Iy(x, y);
        Ixy(x, y) = Ix(x, y) * Iy(x, y);
        
        // Box filter 3x3
        Func Sxx("Sxx"), Syy("Syy"), Sxy("Sxy");
        Sxx(x, y) = Ixx(x - 1, y - 1) + Ixx(x - 1, y) + Ixx(x - 1, y + 1) +
                     Ixx(x, y - 1) + Ixx(x, y) + Ixx(x, y + 1) +
                     Ixx(x + 1, y - 1) + Ixx(x + 1, y) + Ixx(x + 1, y + 1);
        Syy(x, y) = Iyy(x - 1, y - 1) + Iyy(x - 1, y) + Iyy(x - 1, y + 1) +
                     Iyy(x, y - 1) + Iyy(x, y) + Iyy(x, y + 1) +
                     Iyy(x + 1, y - 1) + Iyy(x + 1, y) + Iyy(x + 1, y + 1);
        Sxy(x, y) = Ixy(x - 1, y - 1) + Ixy(x - 1, y) + Ixy(x - 1, y + 1) +
                     Ixy(x, y - 1) + Ixy(x, y) + Ixy(x, y + 1) +
                     Ixy(x + 1, y - 1) + Ixy(x + 1, y) + Ixy(x + 1, y + 1);
        
        // Harris response
        Func det("det"), trace("trace");
        det(x, y) = Sxx(x, y) * Syy(x, y) - Sxy(x, y) * Sxy(x, y);
        trace(x, y) = Sxx(x, y) + Syy(x, y);
        output(x, y) = det(x, y) - 0.04f * trace(x, y) * trace(x, y);
    }
};

HALIDE_REGISTER_GENERATOR(Harris, harris)
