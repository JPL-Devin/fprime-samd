// ======================================================================
// \title  PassiveSchedAdapter.cpp
// \brief  Svc.PassiveCycler turn -> Svc.Sched fan-out, always "no more work"
// ======================================================================

#include "fprime-samd/Svc/PassiveSchedAdapter/PassiveSchedAdapter.hpp"

namespace Svc {

PassiveSchedAdapter ::PassiveSchedAdapter(const char* const compName) : PassiveSchedAdapterComponentBase(compName) {}

PassiveSchedAdapter ::~PassiveSchedAdapter() {}

bool PassiveSchedAdapter ::activeIn_handler(FwIndexType portNum, U32 context) {
    for (FwIndexType i = 0; i < this->getNum_schedOut_OutputPorts(); i++) {
        if (this->isConnected_schedOut_OutputPort(i)) {
            this->schedOut_out(i, context);
        }
    }
    // One pass per cycle: members that latched more work pick it up on the next turn, which
    // the cycler takes as soon as the next interrupt (including the DMAC) wakes the core.
    return false;
}

}  // namespace Svc
