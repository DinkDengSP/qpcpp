//$file${src::qxk::qxk_mutex.cpp} vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
//
// Model: qpcpp.qm
// File:  ${src::qxk::qxk_mutex.cpp}
//
// This code has been generated by QM 5.2.1 <www.state-machine.com/qm>.
// DO NOT EDIT THIS FILE MANUALLY. All your changes will be lost.
//
// This code is covered by the following QP license:
// License #    : LicenseRef-QL-dual
// Issued to    : Any user of the QP/C++ real-time embedded framework
// Framework(s) : qpcpp
// Support ends : 2023-12-31
// License scope:
//
// Copyright (C) 2005 Quantum Leaps, LLC <state-machine.com>.
//
// SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-QL-commercial
//
// This software is dual-licensed under the terms of the open source GNU
// General Public License version 3 (or any later version), or alternatively,
// under the terms of one of the closed source Quantum Leaps commercial
// licenses.
//
// The terms of the open source GNU General Public License version 3
// can be found at: <www.gnu.org/licenses/gpl-3.0>
//
// The terms of the closed source Quantum Leaps commercial licenses
// can be found at: <www.state-machine.com/licensing>
//
// Redistributions in source code must retain this top-level comment block.
// Plagiarizing this software to sidestep the license obligations is illegal.
//
// Contact information:
// <www.state-machine.com/licensing>
// <info@state-machine.com>
//
//$endhead${src::qxk::qxk_mutex.cpp} ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
//! @file
//! @brief Priority-ceiling blocking mutex QP::QXMutex class definition

#define QP_IMPL             // this is QP implementation
#include "qf_port.hpp"      // QF port
#include "qf_pkg.hpp"       // QF package-scope internal interface
#include "qassert.h"        // QP embedded systems-friendly assertions
#ifdef Q_SPY                // QS software tracing enabled?
    #include "qs_port.hpp"  // QS port
    #include "qs_pkg.hpp"   // QS facilities for pre-defined trace records
#else
    #include "qs_dummy.hpp" // disable the QS software tracing
#endif // Q_SPY

// protection against including this source file in a wrong project
#ifndef QXK_HPP
#error "Source file included in a project NOT based on the QXK kernel"
#endif // QXK_HPP

//============================================================================
namespace { // unnamed local namespace
Q_DEFINE_THIS_MODULE("qxk_mutex")
} // unnamed namespace

//$skip${QP_VERSION} vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
// Check for the minimum required QP version
#if (QP_VERSION < 690U) || (QP_VERSION != ((QP_RELEASE^4294967295U) % 0x3E8U))
#error qpcpp version 6.9.0 or higher required
#endif
//$endskip${QP_VERSION} ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

//$define${QXK::QXMutex} vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
namespace QP {

//${QXK::QXMutex} ............................................................

//${QXK::QXMutex::QXMutex} ...................................................
QXMutex::QXMutex()
  : QActive(Q_STATE_CAST(0))
{}

//${QXK::QXMutex::init} ......................................................
void QXMutex::init(QPrioSpec const prioSpec) noexcept {
    m_prio  = static_cast<std::uint8_t>(prioSpec & 0xFFU);
    m_pthre = static_cast<std::uint8_t>(prioSpec >> 8U);

    if (prioSpec != 0U) {  // priority-ceiling protocol used?
        register_();  // register this mutex as AO
    }
}

//${QXK::QXMutex::tryLock} ...................................................
bool QXMutex::tryLock() noexcept {
    QF_CRIT_STAT_
    QF_CRIT_E_();

    QActive *curr = QXK_attr_.curr;
    if (curr == nullptr) { // called from a basic thread?
        curr = registry_[QXK_attr_.actPrio];
    }

    //! @pre this function must:
    //! - NOT be called from an ISR;
    //! - the calling thread must be valid;
    //! - the mutex-priority must be in range
    Q_REQUIRE_ID(300, (!QXK_ISR_CONTEXT_()) // don't call from an ISR!
        && (curr != nullptr) // current thread must be valid
        && (m_prio <= QF_MAX_ACTIVE));
    //! @pre also: the thread must NOT be holding a scheduler lock.
    Q_REQUIRE_ID(301, QXK_attr_.lockHolder != curr->m_prio);

    // is the mutex available?
    if (m_eQueue.m_nFree == 0U) {
        m_eQueue.m_nFree = 1U;  // mutex lock nesting

        //! @pre also: the newly locked mutex must have no holder yet
        Q_REQUIRE_ID(302, m_thread == nullptr);

        // set the new mutex holder to the curr thread and
        // save the thread's prio/pthre in the mutex
        // NOTE: reuse the otherwise unused eQueue data member.
        m_thread = curr;
        m_eQueue.m_head = static_cast<QEQueueCtr>(curr->m_prio);
        m_eQueue.m_tail = static_cast<QEQueueCtr>(curr->m_pthre);

        QS_BEGIN_NOCRIT_PRE_(QS_MTX_LOCK, curr->m_prio)
            QS_TIME_PRE_();  // timestamp
            QS_OBJ_PRE_(this); // this mutex
            QS_2U8_PRE_(static_cast<std::uint8_t>(m_eQueue.m_head),
                        static_cast<std::uint8_t>(m_eQueue.m_nFree));
        QS_END_NOCRIT_PRE_()

        if (m_prio != 0U) { // priority-ceiling protocol used?
            // the holder priority must be lower than that of the mutex
            // and the priority slot must be occupied by this mutex
            Q_ASSERT_ID(210, (curr->m_prio < m_prio)
                && (registry_[m_prio] == this));

            // remove the thread's original prio from the ready set
            // and insert the mutex's prio into the ready set
            QF::readySet_.remove(
                static_cast<std::uint_fast8_t>(m_eQueue.m_head));
            QF::readySet_.insert(
                static_cast<std::uint_fast8_t>(m_prio));

            // put the thread into the AO registry in place of the mutex
            registry_[m_prio] = curr;

            // set thread's prio/pthre to that of the mutex
            curr->m_prio  = m_prio;
            curr->m_pthre = m_pthre;
        }
    }
    // is the mutex locked by this thread already (nested locking)?
    else if (m_thread == curr) {
        // the nesting level must not exceed the specified limit
        Q_ASSERT_ID(320, m_eQueue.m_nFree < 0xFFU);

        m_eQueue.m_nFree = m_eQueue.m_nFree + 1U; // lock one more level

        QS_BEGIN_NOCRIT_PRE_(QS_MTX_LOCK, curr->m_prio)
            QS_TIME_PRE_();  // timestamp
            QS_OBJ_PRE_(this); // this mutex
            QS_2U8_PRE_(static_cast<std::uint8_t>(m_eQueue.m_head),
                        static_cast<std::uint8_t>(m_eQueue.m_nFree));
        QS_END_NOCRIT_PRE_()
    }
    else { // the mutex is already locked by a different thread
        if (m_prio != 0U) {  // priority-ceiling protocol used?
            // the prio slot must be occupied by the thr. holding the mutex
            Q_ASSERT_ID(340, registry_[m_prio]
                             == QXK_PTR_CAST_(QActive *, m_thread));
        }

        QS_BEGIN_NOCRIT_PRE_(QS_MTX_BLOCK_ATTEMPT, curr->m_prio)
            QS_TIME_PRE_();  // timestamp
            QS_OBJ_PRE_(this); // this mutex
            QS_2U8_PRE_(static_cast<std::uint8_t>(m_eQueue.m_head),
                        curr->m_prio); // trying thread prio
        QS_END_NOCRIT_PRE_()

        curr = nullptr; // means that mutex is NOT available
    }
    QF_CRIT_X_();

    return curr != nullptr;
}

//${QXK::QXMutex::lock} ......................................................
bool QXMutex::lock(std::uint_fast16_t const nTicks) noexcept {
    QF_CRIT_STAT_
    QF_CRIT_E_();

    QXThread * const curr = QXK_PTR_CAST_(QXThread*, QXK_attr_.curr);

    //! @pre this function must:
    //! - NOT be called from an ISR;
    //! - be called from an extended thread;
    //! - the mutex-priority must be in range
    //! - the thread must NOT be already blocked on any object.

    Q_REQUIRE_ID(200, (!QXK_ISR_CONTEXT_()) // don't call from an ISR!
    && (curr != nullptr) // current thread must be extended
    && (m_prio <= QF_MAX_ACTIVE)
    && (curr->m_temp.obj == nullptr)); // not blocked
    //! @pre also: the thread must NOT be holding a scheduler lock
    Q_REQUIRE_ID(201, QXK_attr_.lockHolder != curr->m_prio);

    // is the mutex available?
    bool locked = true; // assume that the mutex will be locked
    if (m_eQueue.m_nFree == 0U) {
        m_eQueue.m_nFree = 1U; // mutex lock nesting

        //! @pre also: the newly locked mutex must have no holder yet
        Q_REQUIRE_ID(202, m_thread == nullptr);

        // set the new mutex holder to the curr thread and
        // save the thread's prio/pthre in the mutex
        // NOTE: reuse the otherwise unused eQueue data member.
        m_thread = curr;
        m_eQueue.m_head = static_cast<QEQueueCtr>(curr->m_prio);
        m_eQueue.m_tail = static_cast<QEQueueCtr>(curr->m_pthre);

        QS_BEGIN_NOCRIT_PRE_(QS_MTX_LOCK, curr->m_prio)
            QS_TIME_PRE_();  // timestamp
            QS_OBJ_PRE_(this); // this mutex
            QS_2U8_PRE_(static_cast<std::uint8_t>(m_eQueue.m_head),
                        static_cast<std::uint8_t>(m_eQueue.m_nFree));
        QS_END_NOCRIT_PRE_()

        if (m_prio != 0U) { // priority-ceiling protocol used?
            // the holder priority must be lower than that of the mutex
            // and the priority slot must be occupied by this mutex
            Q_ASSERT_ID(210, (curr->m_prio < m_prio)
                && (registry_[m_prio] == this));

            // remove the thread's original prio from the ready set
            // and insert the mutex's prio into the ready set
            QF::readySet_.remove(
                static_cast<std::uint_fast8_t>(m_eQueue.m_head));
            QF::readySet_.insert(static_cast<std::uint_fast8_t>(m_prio));

            // put the thread into the AO registry in place of the mutex
            registry_[m_prio] = curr;

            // set thread's prio/pthre to that of the mutex
            curr->m_prio  = m_prio;
            curr->m_pthre = m_pthre;
        }
    }
    // is the mutex locked by this thread already (nested locking)?
    else if (m_thread == curr) {

        // the nesting level beyond the arbitrary but high limit
        // most likely means cyclic or recursive locking of a mutex.
        Q_ASSERT_ID(220, m_eQueue.m_nFree < 0xFFU);

        m_eQueue.m_nFree = m_eQueue.m_nFree + 1U; // lock one more level

        QS_BEGIN_NOCRIT_PRE_(QS_MTX_LOCK, curr->m_prio)
            QS_TIME_PRE_();  // timestamp
            QS_OBJ_PRE_(this); // this mutex
            QS_2U8_PRE_(static_cast<std::uint8_t>(m_eQueue.m_head),
                        static_cast<std::uint8_t>(m_eQueue.m_nFree));
        QS_END_NOCRIT_PRE_()
    }
    else { // the mutex is already locked by a different thread
        // the mutex holder must be valid
        Q_ASSERT_ID(230, m_thread != nullptr);

        if (m_prio != 0U) { // priority-ceiling protocol used?
            // the prio slot must be occupied by the thr. holding the mutex
            Q_ASSERT_ID(240, registry_[m_prio]
                             == QXK_PTR_CAST_(QActive *, m_thread));
        }

        // remove the curr thread's prio from the ready set (will block)
        // and insert it to the waiting set on this mutex
        std::uint_fast8_t const p =
            static_cast<std::uint_fast8_t>(curr->m_prio);
        QF::readySet_.remove(p);
        m_waitSet.insert(p);

        // set the blocking object (this mutex)
        curr->m_temp.obj = QXK_PTR_CAST_(QMState*, this);
        curr->teArm_(static_cast<enum_t>(QXK::MUTEX_SIG), nTicks);

        QS_BEGIN_NOCRIT_PRE_(QS_MTX_BLOCK, curr->m_prio)
            QS_TIME_PRE_();  // timestamp
            QS_OBJ_PRE_(this); // this mutex
            QS_2U8_PRE_(static_cast<std::uint8_t>(m_eQueue.m_head),
                        curr->m_prio);
        QS_END_NOCRIT_PRE_()

        // schedule the next thread if multitasking started
        static_cast<void>(QXK_sched_());
        QF_CRIT_X_();
        QF_CRIT_EXIT_NOP(); // BLOCK here !!!

        // AFTER unblocking...
        QF_CRIT_E_();
        // the blocking object must be this mutex
        Q_ASSERT_ID(240, curr->m_temp.obj
                         == QXK_PTR_CAST_(QMState*, this));

        // did the blocking time-out? (signal of zero means that it did)
        if (curr->m_timeEvt.sig == 0U) {
            if (m_waitSet.hasElement(p)) { // still waiting?
                m_waitSet.remove(p); // remove unblocked thread
                locked = false; // the mutex was NOT locked
            }
        }
        else { // blocking did NOT time out
            // the thread must NOT be waiting on this mutex
            Q_ASSERT_ID(250, !m_waitSet.hasElement(p));
        }
        curr->m_temp.obj = nullptr; // clear blocking obj.
    }
    QF_CRIT_X_();

    return locked;
}

//${QXK::QXMutex::unlock} ....................................................
void QXMutex::unlock() noexcept {
    QF_CRIT_STAT_
    QF_CRIT_E_();

    QActive *curr = QXK_attr_.curr;
    if (curr == nullptr) { // called from a basic thread?
        curr = registry_[QXK_attr_.actPrio];
    }

    //! @pre this function must:
    //! - NOT be called from an ISR;
    //! - the calling thread must be valid;
    Q_REQUIRE_ID(400, (!QXK_ISR_CONTEXT_()) // don't call from an ISR!
        && (curr != nullptr)); // current thread must be valid

    //! @pre also: the mutex must be already locked at least once
    Q_REQUIRE_ID(401, m_eQueue.m_nFree > 0U);
    //! @pre also: the mutex must be held by this thread
    Q_REQUIRE_ID(402, m_thread == curr);

    // is this the last nesting level?
    if (m_eQueue.m_nFree == 1U) {

        if (m_prio != 0U) { // priority-ceiling protocol used?

            // restore the holding thread's prio/pthre from the mutex
            curr->m_prio  = static_cast<std::uint8_t>(m_eQueue.m_head);
            curr->m_pthre = static_cast<std::uint8_t>(m_eQueue.m_tail);

            // put the mutex back into the AO registry
            registry_[m_prio] = this;

            // remove the mutex' prio from the ready set
            // and insert the original thread's priority
            QF::readySet_.remove(
                static_cast<std::uint_fast8_t>(m_prio));
            QF::readySet_.insert(
                static_cast<std::uint_fast8_t>(m_eQueue.m_head));
        }

        QS_BEGIN_NOCRIT_PRE_(QS_MTX_UNLOCK, curr->m_prio)
            QS_TIME_PRE_();  // timestamp
            QS_OBJ_PRE_(this); // this mutex
            QS_2U8_PRE_(static_cast<std::uint8_t>(m_eQueue.m_head),
                        0U);
        QS_END_NOCRIT_PRE_()

        // are any other threads waiting on this mutex?
        if (m_waitSet.notEmpty()) {
            // find the highest-priority thread waiting on this mutex
            std::uint_fast8_t const p = m_waitSet.findMax();

            // remove this thread from waiting on the mutex
            // and insert it into the ready set.
            m_waitSet.remove(p);
            QF::readySet_.insert(p);

            QXThread * const thr = QXK_PTR_CAST_(QXThread*, registry_[p]);

            // the waiting thread must:
            // - be registered in QF
            // - have the priority corresponding to the registration
            // - be an extended thread
            // - be blocked on this mutex
            Q_ASSERT_ID(410, (thr != (QXThread *)0)
                && (thr->m_prio == static_cast<std::uint8_t>(p))
                && (thr->m_state.act == Q_ACTION_CAST(0))
                && (thr->m_temp.obj == QXK_PTR_CAST_(QMState*, this)));

            // disarm the internal time event
            static_cast<void>(thr->teDisarm_());

            // set the new mutex holder to the curr thread and
            // save the thread's prio/pthre in the mutex
            // NOTE: reuse the otherwise unused eQueue data member.
            m_thread = thr;
            m_eQueue.m_head = static_cast<QEQueueCtr>(thr->m_prio);
            m_eQueue.m_tail = static_cast<QEQueueCtr>(thr->m_pthre);

            QS_BEGIN_NOCRIT_PRE_(QS_MTX_LOCK, thr->m_prio)
                QS_TIME_PRE_();  // timestamp
                QS_OBJ_PRE_(this); // this mutex
                QS_2U8_PRE_(static_cast<std::uint8_t>(m_eQueue.m_head),
                            static_cast<std::uint8_t>(m_eQueue.m_nFree));
            QS_END_NOCRIT_PRE_()

            if (m_prio != 0U) { // priority-ceiling protocol used?
                // the holder priority must be lower than that of the mutex
                Q_ASSERT_ID(410, thr->m_prio < m_prio);

                // set thread's preemption-threshold to that of the mutex
                thr->m_pthre = m_pthre;

                // put the thread into AO registry in place of the mutex
                registry_[m_prio] = thr;
            }
        }
        else { // no threads are waiting for this mutex
            m_eQueue.m_nFree = 0U; // free up the nesting count

            // the mutex no longer held by any thread
            m_thread = nullptr;
            m_eQueue.m_head = 0U;
            m_eQueue.m_tail = 0U;

            if (m_prio != 0U) { // priority-ceiling protocol used?
                // put the mutex back at the original mutex slot
                registry_[m_prio] = QXK_PTR_CAST_(QActive*, this);
            }
        }

        // schedule the next thread if multitasking started
        if (QXK_sched_() != 0U) {
            QXK_activate_(); // activate a basic thread
        }
    }
    else { // releasing one level of nested mutex lock
        Q_ASSERT_ID(420, m_eQueue.m_nFree > 0U);
        m_eQueue.m_nFree = m_eQueue.m_nFree - 1U; // unlock one level

        QS_BEGIN_NOCRIT_PRE_(QS_MTX_UNLOCK_ATTEMPT, curr->m_prio)
            QS_TIME_PRE_();  // timestamp
            QS_OBJ_PRE_(this); // this mutex
            QS_2U8_PRE_(static_cast<std::uint8_t>(m_eQueue.m_head),
                        static_cast<std::uint8_t>(m_eQueue.m_nFree));
        QS_END_NOCRIT_PRE_()
    }
    QF_CRIT_X_();
}

} // namespace QP
//$enddef${QXK::QXMutex} ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
