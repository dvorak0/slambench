// Harris Generator - AOT compiled Harris
// Follows lesson_21 pattern

#include "Halide.h"

using namespace Halide;

class Harris : public Generator<Harris> {
public:
    Input<Buffer<uint8_t, 2>> input{"input"};
    Output<Buffer<float, 2>> output{"output"};
    
    void generate() {
        Var x("x"), y("y");
        
        // Boundary conditions
        Func in_f("in_f");
        in_f(x, y) = BoundaryConditions::repeat_edge(input)(x, y);
        
        // Sobel gradients
        Func Iy("Iy"), Ix("Ix");
        Iy(x, y) = in_f(x - 1, y - 1) * (-1.0f/12.0f) + in_f(x - 1, y + 1) * (1.0f/12.0f) +
                   in_f(x, y - 1) * (-2.0f/12.0f) + in_f(x, y + 1) * (2.0f/12.0f) +
                   in_f(x + 1, y - 1) * (-1.0f/12.0f) + in_f(x + 1, y + 1) * (1.0f/12.0f);
        Ix(x, y) = in_f(x - 1, y - 1) * (-1.0f/12.0f) + in_f(x + 1, y - 1) * (1.0f/12.0f) +
                   in_f(x - 1, y) * (-2.0f/12.0f) + in_f(x + 1, y) * (2.0f/12.0f) +
                   in_f(x - 1, y + 1) * (-1.0f/12.0f) + in_f(x + 1, y + 1) * (1.0f/12.0f);
        
        // Covariance
        Func Ixx("Ixx"), Iyy("Iyy"), Ixy("Ixy");
        Ixx(x, y) = Ix(x, y) * Ix(x, y);
        Iyy(x, y) = Iy(x, y) * Iy(x, y);
        Ixy(x, y) = Ix(x, y) * Iy(x, y);
        
        // Box filter 3x3
        Func Sxx("Sxx"), Syy("Syy"), Sxy("Sxy");
        Sxx(x, y) = Ixx(x-1,y-1) + Ixx(x-1,y) + Ixx(x-1,y+1) + Ixx(x,y-1) + Ixx(x,y) + Ixx(x,y+1) + Ixx(x+1,y-1) + Ixx(x+1,y) + Ixx(x+1,y+1);
        Syy(x, y) = Iyy(x-1,y-1) + Iyy(x-1,y) + Iyy(x-1,y+1) + Iyy(x,y-1) + Iyy(x,y) + Iyy(x,y+1) + Iyy(x+1,y-1) + Iyy(x+1,y) + Iyy(x+1,y+1);
        Sxy(x, y) = Ixy(x-1,y-1) + Ixy(x-1,y) + Ixy(x-1,y+1) + Ixy(x,y-1) + Ixy(x,y) + Ixy(x,y+1) + Ixy(x+1,y-1) + Ixy(x+1,y) + Ixy(x+1,y+1);
        
        // Harris response
        output(x, y) = (Sxx(x,y)*Syy(x,y) - Sxy(x,y)*Sxy(x,y)) - 0.04f * (Sxx(x,y) + Syy(x,y)) * (Sxx(x,y) + Syy(x,y));
    }
    
    void schedule() {
        // Manual schedule
        Var x("x"), y("y"), yi;
        output.split(y, y, yi, 32).parallel(y).vectorize(x, 8);
    }
};

HALIDE_REGISTER_GENERATOR(Harris, harris)
