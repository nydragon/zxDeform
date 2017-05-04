#include "zxtimestepperbackwardeuler.h"

zxTimeStepperBackwardEuler::zxTimeStepperBackwardEuler()
{

}

zxTimeStepperBackwardEuler::zxTimeStepperBackwardEuler(zxForceModel::Ptr forcemodel)
    :zxTimeStepper(forcemodel)
{
    m_stiff_spmat = forcemodel->getStiffMatrixTopology();
    m_sys_spmat = m_stiff_spmat;

    zxMesh::Ptr mesh = m_forceModel->get_mesh();
    zxMaterial::Ptr material = m_forceModel->get_material();

    std::vector<real> vertex_mass(mesh->get_num_nodes(),0);
    for(size_t el = 0; el < mesh->get_num_elements(); el++)
    {
        zxElement::Ptr element = mesh->get_element(el);
        real mass = element->get_volume() * material->get_density();
        mass /= element->get_num_nodes();
        for(size_t i = 0; i < element->get_num_nodes(); i++)
            vertex_mass[mesh->get_node(el,i)->m_id] += mass;
    }
    m_mass_spmat.resize(3 * mesh->get_num_nodes(),3 * mesh->get_num_nodes());
    m_mass_spmat.setIdentity();
    for(size_t i = 0; i < mesh->get_num_nodes(); i++)
        for(int j = 0; j < 3; j++)
        {
            m_mass_spmat.coeffRef(3 * i + j,3 * i + j) = vertex_mass[i];
            m_ext_force[3 * i + 1] = vertex_mass[i] * -9.81;
        }
}

void zxTimeStepperBackwardEuler::do_step(real dt)
{
    zxMesh::Ptr mesh = m_forceModel->get_mesh();
    for(size_t i = 0; i < mesh->get_num_nodes(); i++)
    {
        zxNode::Ptr node = mesh->get_node(i);
        node->rp = node->rt;
    }

    do_step_sparse(dt);

}


void zxTimeStepperBackwardEuler::do_step_sparse(real dt)
{
    m_dt = dt;
    m_pos0 = m_pos;
    m_vel0 = m_vel;

    //m_pos += m_vel * dt;


    update_position(m_pos);
    compute_hessian(m_sys_spmat);
    compute_residual(m_residual);
    compute_delta_position(m_dpos);

    m_pos -= m_dpos;
    m_vel = -1.0/dt * m_dpos;

    m_tem_pos = m_pos;
}

void zxTimeStepperBackwardEuler::compute_hessian(zxSparseMatrix &H)
{
    m_forceModel->computeTangent(H);

    for(size_t i = 0; i < m_dim; i++)
    {
        H.coeffRef(i,i) += m_mass_spmat.coeffRef(i,i) / (m_dt * m_dt);
    }

    for(size_t i = 0; i < m_fixedDof.size(); i++)
        H.coeffRef(m_fixedDof[i],m_fixedDof[i]) = 1e40;
}

void zxTimeStepperBackwardEuler::compute_residual(Eigen::VectorXd &dres)
{
    m_forceModel->computeForce(m_int_force);
    dres = m_int_force - m_ext_force + m_mass_spmat * ( (m_pos - m_pos0) / m_dt - m_vel0) / m_dt;
    for(size_t i = 0; i < m_fixedDof.size(); i++)
        dres[m_fixedDof[i]] = 0.0;
}

void zxTimeStepperBackwardEuler::compute_delta_position(Eigen::VectorXd &dp)
{
    Eigen::ConjugateGradient<Eigen::SparseMatrix<real>> solver;
    solver.setMaxIterations(20);
    solver.compute(m_sys_spmat);
    dp = solver.solve(m_residual);
}

void zxTimeStepperBackwardEuler::update_position(Eigen::VectorXd &q)
{
    m_forceModel->updatePosition(q);
}

void zxTimeStepperBackwardEuler::reset_iac()
{
    zxMesh::Ptr mesh = m_forceModel->get_mesh();
    for(size_t i = 0; i < mesh->get_num_nodes(); i++)
    {
        zxNode::Ptr node = mesh->get_node(i);
        node->reset_iac();
        mat3d Aii;
        for(size_t ii = 0; ii < 3; ii++)
            for(size_t jj = 0; jj < 3; jj++)
                Aii(ii,jj) = m_sys_spmat.coeff(3 * i + ii, 3 * i + jj);
        node->m_lcp_invA = Aii.inverse();
    }

    m_tem_pos = m_pos;

}
void zxTimeStepperBackwardEuler::compute_iac_response_gs(Eigen::VectorXd &x, Eigen::VectorXd &b,Eigen::VectorXd &r)
{
    r = b - m_sys_spmat * x;
    zxMesh::Ptr mesh = m_forceModel->get_mesh();
    int* colptr = m_sys_spmat.outerIndexPtr();
    int* rowinptr = m_sys_spmat.innerIndexPtr();
    real* value = m_sys_spmat.valuePtr();
    for(size_t i = 0; i < m_dim/3; i++)
    {
        vec3d ri(r[3 * i + 0],r[3 * i + 1],r[3 * i + 2]);
        vec3d xi = mesh->get_node(i)->m_lcp_invA * ri;

        for(int j = 0; j < 3; j++)
        {
            x[3 * i + j] += xi[j];
            int c = 3 * i + j;

            for(int k = 0; k < colptr[c + 1] - colptr[c]; k++)
            {
                int rowind = rowinptr[colptr[c] + k];
                real Aij = value[colptr[c] + k];
                r[rowind] -= Aij * xi[j];
            }

        }
    }
}

void zxTimeStepperBackwardEuler::compute_iac_response()
{
    zxMesh::Ptr mesh = m_forceModel->get_mesh();
    Eigen::VectorXd lcp_imp(3 * mesh->get_num_nodes()),lcp_dx(3 * mesh->get_num_nodes()),lcp_imp_res(3 * mesh->get_num_nodes());
    for(size_t i = 0; i < mesh->get_num_nodes(); i++)
    {
        zxNode::Ptr node = mesh->get_node(i);

        for(size_t j = 0; j < 3; j++)
        {
            lcp_imp[3 * i + j] = node->m_lcp_imp[j];
            lcp_dx[3 * i + j] = node->m_lcp_dx[j];
        }
    }
    //Eigen::ConjugateGradient<Eigen::SparseMatrix<real>> solver(m_sys_spmat);
    //solver.setMaxIterations(2);
    //lcp_dx = solver.solveWithGuess(lcp_imp,lcp_dx);

    compute_iac_response_gs(lcp_dx,lcp_imp,lcp_imp_res);

    for(size_t i = 0; i < mesh->get_num_nodes(); i++)
    {
        zxNode::Ptr node = mesh->get_node(i);

        for(size_t j = 0; j < 3; j++)
        {
            node->m_lcp_dx[j] = lcp_dx[3 * i + j];
            node->m_lcp_res_imp[j] = lcp_imp_res[3 * i + j];
        }
    }
    m_pos = m_tem_pos + lcp_dx;
    m_vel = (m_pos - m_pos0) / m_dt;
    int i = 0;

}


