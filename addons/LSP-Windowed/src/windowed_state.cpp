#include "windowed_state.hpp"

WindowedState& GetState() {
    static WindowedState s;
    return s;
}
