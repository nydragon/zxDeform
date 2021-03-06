#ifndef ZXLCPCONSTRAINT_H
#define ZXLCPCONSTRAINT_H

#include "zxbasicgeometry.h"
class zxLCPConstraint
{
    ZX_MAKE_SHARED_MACO_DEFAULT_CREAT(zxLCPConstraint)
public:
    zxLCPConstraint();
    virtual real get_residual();
    virtual real get_lambda(){return m_lambda;}
    virtual void reset_projected_gs();
    virtual void do_projected_gs_solve();
    virtual void set_lcp_impulse();
    virtual void zero_lcp_impulse();

    virtual void init();

    virtual void compute_alm_Ab();
    virtual void do_alm_augment();
    virtual void set_alm_impulse();


public:
    std::vector<zxNode::Ptr> get_nodes(){return m_nodes;}
    std::vector<real>        get_weights(){return m_weights;}
    void                     set_penalty(real penalty){m_penalty = penalty;}



protected:
    std::vector<zxNode::Ptr> m_nodes;
    std::vector<vec3d>       m_jac;
    std::vector<real>        m_weights;
    real                     m_cb,m_invJtAJ;
    real                     m_lambda;
    real                     m_upper_limits,m_lower_limits;
    real                     m_penalty;
};

#endif // ZXLCPCONSTRAINT_H
