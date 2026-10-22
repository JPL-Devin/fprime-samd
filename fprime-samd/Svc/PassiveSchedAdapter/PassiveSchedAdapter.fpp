module Svc {
    @ Turns one Svc.PassiveCycler turn into Svc.Sched calls, so that components written against
    @ the framework scheduler port (Svc.Sched) can run on a cycler-driven baremetal deployment
    @ without knowing about Svc.ActiveSched. Each turn fans out to every connected schedOut in
    @ index order and reports no further work (returns false): the members get exactly one
    @ pass per cycle.
    passive component PassiveSchedAdapter {

        @ One turn of the passive cycler; always returns false (no more work this cycle)
        sync input port activeIn: Svc.ActiveSched

        @ The scheduled members, called in index order with the cycler's context
        output port schedOut: [PassiveRateGroupOutputPorts] Svc.Sched

    }
}
