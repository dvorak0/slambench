// Harris Generator - AOT compiled Harris
// Supports both manual and auto schedule via GENERATOR_MODE

#include "Halide.h"

using namespace Halide;

class Harris : public Generator<Harris> {
public:
    // 0 = no schedule, 1 = manual schedule
    GeneratorParam<int> generator_mode{"generator_mode", 1};
    
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
        
        // Estimates for auto-scheduler
        input.dim(0).set_estimate(0, 752);
        input.dim(1).set_estimate(0, 480);
        output.set_estimate(x, 0, 752);
        output.set_estimate(y, 0, 480);
    }
    
    void schedule() {
        if (generator_mode == 1) {
            // Manual schedule
            Var x("x"), y("y"), yi;
            output.split(y, y, yi, 32).parallel(y).vectorize(x, 8);
        }
        // generator_mode == 0 means no schedule (for auto-scheduler)
    }
};

HALIDE_REGISTER_GENERATOR(Harris, harris)
