//
// Created by yasu on 26/10/18.
//

#include "Manager.h"

// taken from https://gist.github.com/javidcf/25066cf85e71105d57b6
template<class MatT>
Eigen::Matrix<typename MatT::Scalar, MatT::ColsAtCompileTime, MatT::RowsAtCompileTime>
pseudoinverse(const MatT &mat, typename MatT::Scalar tolerance = typename MatT::Scalar{1e-4}) // choose appropriately
{
    typedef typename MatT::Scalar Scalar;
    auto svd = mat.jacobiSvd(Eigen::ComputeFullU | Eigen::ComputeFullV);
    const auto &singularValues = svd.singularValues();
    Eigen::Matrix<Scalar, MatT::ColsAtCompileTime, MatT::RowsAtCompileTime> singularValuesInv(mat.cols(), mat.rows());
    singularValuesInv.setZero();
    for (unsigned int i = 0; i < singularValues.size(); ++i) {
        if (singularValues(i) > tolerance) {
            singularValuesInv(i, i) = Scalar{1} / singularValues(i);
        } else {
            singularValuesInv(i, i) = Scalar{0};
        }
    }
    return svd.matrixV() * singularValuesInv * svd.matrixU().adjoint();
}

Manager::Manager(bool logMode, bool use_pid) : logMode(logMode), use_pid(use_pid) {
    std::cout << "Setting up Manager...\n";
    // set up CurvatureCalculator, AugmentedRigidArm, and ControllerPCC objects.
    softArm = new SoftArm{};
    augmentedRigidArm = new AugmentedRigidArm{};
    controllerPCC = new ControllerPCC{augmentedRigidArm, softArm};

    logBeginTime = std::chrono::high_resolution_clock::now();
    std::cout << "Setup of Manager done.\n";
}

void Manager::curvatureControl(Vector2Nd q,
                               Vector2Nd dq,
                               Vector2Nd ddq) {
    // gets values from ControllerPCC and relays that to SoftArm
    if (use_pid) {
        Vector2Nd output;
        controllerPCC->curvaturePIDControl(q, &output);
        softArm->actuate(output, true);
    } else {
        Vector2Nd f;
        controllerPCC->curvatureDynamicControl(q, dq, ddq, &f);
        softArm->actuate(f);
    }
    if (logMode)
        log(softArm->curvatureCalculator->q, q);
}

void Manager::sendJointSpaceProfile(vFunctionCall updateQ, double duration) {
    std::chrono::high_resolution_clock::time_point lastTime; // used to keep track of how long the control loop took
    int count=0;
    Vector2Nd q=Vector2Nd::Zero();
    Vector2Nd q_tmp1=Vector2Nd::Zero();
    Vector2Nd q_tmp2=Vector2Nd::Zero();
    Vector2Nd dq=Vector2Nd::Zero();
    Vector2Nd ddq=Vector2Nd::Zero();
    double epsilon = 0.01;
    long long sum_duration = 0;
    int duration_control;

    for (double seconds = 0; seconds < duration; seconds += CONTROL_PERIOD) {
        count++;
        lastTime = std::chrono::high_resolution_clock::now();

        // update q
        updateQ(seconds, &q);
        // numerically derive dq and ddq
        updateQ(seconds+epsilon, &q_tmp1);
        updateQ(seconds+epsilon*2.0, &q_tmp2);
        dq = (q_tmp1 - q)/epsilon;
        ddq = (q_tmp2-2.0*q_tmp1+q)/(epsilon*epsilon);

        curvatureControl(q, dq, ddq);
        duration_control = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - lastTime).count();
        sum_duration += duration_control;
        std::this_thread::sleep_for(std::chrono::microseconds(int(std::fmax(CONTROL_PERIOD * 1000000.0 - duration_control, 0)))); //todo: properly manage time count
    }
    std::cout << "control loop took on average " << sum_duration / count << " microseconds.\n";
}

void Manager::log(Vector2Nd &q_meas, Vector2Nd &q_ref) {
    log_q_meas.push_back(q_meas);
    log_q_ref.push_back(q_ref);
    log_time.push_back(std::chrono::high_resolution_clock::now() - logBeginTime);
    logNum++;
}
void doNothing(double seconds, Vector2Nd * q){

}
void Manager::characterize_part1() {
    std::cout<< "Manager.characterize_part1 called. This measures alpha. Make sure to exert a known force to the tip of the arm(check that it matches with f_ext in code).\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    std::cout << "Doing PID control for 15 seconds to bring arm to a straight position...\n";
    use_pid = true;
    sendJointSpaceProfile((vFunctionCall)doNothing, 15);
    std::cout<<"p=\n"<<softArm->p<<"\n";
    std::cout<<"A_p2f_all * p=\n"<<softArm->A_p2f_all*softArm->p<<"\n";
    Eigen::Matrix<double, 3,1> f_ext = Eigen::Matrix<double, 3,1>::Zero();
    f_ext(0) = 1;
    std::cout << "f_ext=\n" <<f_ext<<"\n";
    Vector2Nd zero_vector = Vector2Nd::Zero();
    controllerPCC->updateBCG(zero_vector, zero_vector);
    std::cout<< " J^T * f_ext=\n" << controllerPCC->J.transpose() * f_ext <<"\n";
    std::cout << "There you go. Please compute alpha from '0 = alpha A_p2f_all * p + J^T * f_ext' yourself, with f being the force exerted to the tip of the arm.\n";//todo: change this
    std::cout << "Put that alpha values to SoftArm.cpp, and run Manager.characterize_part2\n";
}

void Manager::characterize_part2() {

    std::cout << "Manager.characterize_part2 called. This measures k and d. Make sure you've already set the proper value for alpha, using Manager.characterize_part1\n";
    logMode = true;

    const int historySize = 50; // how many samples to use when calculating (make it too big, and pseudoinverse cannot be calculated)
    const double duration = 4; // for how long the process takes
    const int steps = (int) (duration / CONTROL_PERIOD);
    Eigen::MatrixXd pressures;
    pressures.resize(NUM_ELEMENTS * CHAMBERS,
                     steps); // https://stackoverflow.com/questions/23414308/matrix-with-unknown-number-of-rows-and-columns-eigen-library
    const double max_output = 0.5 * (MAX_PRESSURE - PRESSURE_OFFSET);

    // create pressure profile to send to arm. Pressure is monotonically increased then decreased.
    for (int k = 0; k < NUM_ELEMENTS; ++k) {
        for (int j = 0; j < steps; ++j) {
            for (int l = 0; l < CHAMBERS; ++l) {
                pressures(CHAMBERS * k + l, j) = PRESSURE_OFFSET; //first set all to PRESSURE_OFFSET
            }
            pressures(CHAMBERS * k + 0, j) = PRESSURE_OFFSET+fmin(max_output, fmin(max_output * ((double) j * 2 / steps),
                                                             max_output * (2 - (double) j * 2 / steps)));
            pressures(CHAMBERS * k + 1, j) = 0.6*(pressures(CHAMBERS * k + 0, j) -PRESSURE_OFFSET) +PRESSURE_OFFSET;
        }
    }
    // log of pressure (take historySize number of samples)
    Eigen::MatrixXd pressure_log;
    pressure_log.resize(NUM_ELEMENTS * CHAMBERS, historySize);
    // log of q (take historySize number of samples)
    Eigen::MatrixXd q_log;
    q_log.resize(NUM_ELEMENTS * 2, historySize);
    // log of dq (take historySize number of samples)
    Eigen::MatrixXd dq_log;
    dq_log.resize(NUM_ELEMENTS * 2, historySize);

    std::chrono::high_resolution_clock::time_point lastTime;
    int loop_time;
    int log_index = 0;

    // send that to arm and save the results.
    for (int l = 0; l < steps; ++l) {
        lastTime = std::chrono::high_resolution_clock::now();
        log(softArm->curvatureCalculator->q,
            softArm->curvatureCalculator->dq); // hacking the logging mechanism to log dq as well(designed to log commanded q and measured q)
        softArm->actuatePressure(pressures.col(l));
        if (l % (steps / historySize) == 1) {
            // log the current state once every (steps / historySize) steps
            pressure_log.col(log_index) = pressures.col(l);
            q_log.col(log_index) = softArm->curvatureCalculator->q;
            dq_log.col(log_index) = softArm->curvatureCalculator->dq;
            log_index++;
        }
        // control the loop speed here
        loop_time = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - lastTime).count();
        std::this_thread::sleep_for(
                std::chrono::microseconds(int(std::fmax(CONTROL_PERIOD * 1000000 - loop_time - 500, 0))));
    }

    // compute the f for each sample
    Eigen::MatrixXd log_f;
    log_f.resize(2 * NUM_ELEMENTS, historySize);
    for (int l = 0; l < historySize; ++l) {
        controllerPCC->updateBCG(q_log.col(l), dq_log.col(l));
        Vector2Nd tau = /*controllerPCC->C*dq_log.col(l) + */controllerPCC->G - softArm->alpha.asDiagonal()*softArm->A_p2f_all*pressure_log.col(l);
        log_f.col(l) = tau;
    }

    // outputting to CSV format
    std::ofstream output;
    const static Eigen::IOFormat CSVFormat(Eigen::StreamPrecision, Eigen::DontAlignCols, ", ", "\n");
    output.open("./characterization_f.csv");
    output << log_f.format(CSVFormat);
    output.close();
    output.open("./characterization_q.csv");
    output << q_log.format(CSVFormat);
    output.close();
    output.open("./characterization_dq.csv");
    output << dq_log.format(CSVFormat);
    output.close();
    output.open("./characterization_p.csv");
    output << pressure_log.format(CSVFormat);
    output.close();

    std::cout<< "characterization history is saved to csv. Please run Python script for characterization.\n";
}

Manager::~Manager() {
    softArm->stop();
    if (logMode)
        outputLog();
}

void Manager::outputLog() {
    std::cout << "Outputting log to log.csv...\n";
    std::ofstream log_file;
    log_file.open("log.csv");

    // first the header row
    log_file << "time(millis)";
    for (int k = 0; k < NUM_ELEMENTS * 2; ++k) {
        log_file << ", q_ref[" << k << "]";
    }
    for (int k = 0; k < NUM_ELEMENTS * 2; ++k) {
        log_file << ", q_meas[" << k << "]";
    }
    log_file << "\n";

    // log actual data
    for (int j = 0; j < logNum; ++j) {
        log_file << std::chrono::duration_cast<std::chrono::milliseconds>(log_time[j]).count();
        for (int k = 0; k < NUM_ELEMENTS * 2; ++k) {
            log_file << ", " << log_q_ref[j](k);
        }
        for (int k = 0; k < NUM_ELEMENTS * 2; ++k) {
            log_file << ", " << log_q_meas[j](k);
        }
        log_file << "\n";
    }
    log_file.close();
    std::cout << "log output complete.\n";
}
