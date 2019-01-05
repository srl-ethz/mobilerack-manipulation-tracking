//
// Created by yasu and rkk on 26/10/18.
//

#include "ControllerPCC.h"

/**
 * @brief implements a PID controller whose parameters are defined using the Ziegler-Nichols method.
 * @param Ku ultimate gain
 * @param period oscillation period (in seconds)
 * @return MiniPID controller
 */
MiniPID ZieglerNichols(double Ku, double period) {
    // https://en.wikipedia.org/wiki/Ziegler–Nichols_method
//    double Kp = 0.2 * Ku;
//    double Ki = Kp / (period / 2.0) * CONTROL_PERIOD;
//    double Kd = Kp * period / 3.0 / CONTROL_PERIOD;
    double Kp = 0.45*Ku;
    double Ki = Kp / (period/1.2) * CONTROL_PERIOD;
    double Kd = 0.;
    return MiniPID(Kp, Ki, Kd);
}

ControllerPCC::ControllerPCC(AugmentedRigidArm *augmentedRigidArm, SoftArm *softArm) : ara(augmentedRigidArm),
                                                                                       sa(softArm) {
    std::cout<<"ControllerPCC created...\n";
    // set up PID controllers
    for (int j = 0; j < NUM_ELEMENTS*2; ++j) {
        miniPIDs.push_back(ZieglerNichols(30000,0.36));
    }
}

void ControllerPCC::curvatureDynamicControl(const Vector2Nd &q_ref,
                                            const Vector2Nd &dq_ref,
                                            const Vector2Nd &ddq_ref,
                                            Vector2Nd *tau, bool simulate) {
    // variables to save the measured values.
    Vector2Nd q_meas;
    Vector2Nd dq_meas;
    if (USE_FEEDFORWARD_CONTROL or simulate) {
        // don't use the actual values, since it's doing feedforward control.
        q_meas = Vector2Nd(q_ref);
        dq_meas = Vector2Nd(dq_ref);
    } else {
        // get the current configuration from SoftArm.
        q_meas = sa->curvatureCalculator->q;
        dq_meas = sa->curvatureCalculator->dq;
    }
    updateBCG(q_meas, dq_meas);
    *tau = sa->k.asDiagonal() * q_ref + sa->d.asDiagonal() * dq_ref + G + C * dq_ref + B * ddq_ref;
}

void ControllerPCC::updateBCG(const Vector2Nd &q, const Vector2Nd &dq) {
    ara->update(q, dq);
    // these conversions from m space to q space are described in the paper
    B = ara->Jm.transpose() * ara->B_xi * ara->Jm;
    C = ara->Jm.transpose() * ara->B_xi * ara->dJm;
    G = ara->Jm.transpose() * ara->G_xi;
    J = ara->Jxi * ara->Jm;
}

void ControllerPCC::curvaturePIDControl(const Vector2Nd &q_ref, Vector2Nd *pressures) {
    for (int i = 0; i < 2 * NUM_ELEMENTS; ++i) {
        (*pressures)(i) = miniPIDs[i].getOutput(sa->curvatureCalculator->q(i),q_ref(i));
    }
}
