// ======================================================================
// \title  PassiveSchedAdapter.hpp
// \brief  Svc.PassiveCycler turn -> Svc.Sched fan-out, always "no more work"
// ======================================================================

#ifndef Svc_PassiveSchedAdapter_HPP
#define Svc_PassiveSchedAdapter_HPP

#include "fprime-samd/Svc/PassiveSchedAdapter/PassiveSchedAdapterComponentAc.hpp"

namespace Svc {

class PassiveSchedAdapter final : public PassiveSchedAdapterComponentBase {
  public:
    explicit PassiveSchedAdapter(const char* const compName);
    ~PassiveSchedAdapter();

  private:
    //! Calls every connected schedOut in index order; returns false
    bool activeIn_handler(FwIndexType portNum, U32 context) override;
};

}  // namespace Svc

#endif
