IMPLEMENTATION [log]:

#include <cstring>
#include "config.h"
#include "jdb_trace.h"
#include "jdb_tbuf.h"
#include "types.h"
#include "cpu_lock.h"

extern "C" void sys_ipc_wrapper(void);
extern "C" void sys_ipc_log_wrapper(void);


/** IPC logging.
 *  called from interrupt gate.
 */
IMPLEMENT void FIASCO_FLATTEN sys_ipc_log_wrapper()
{
  //panic("sys_ipc_log_wrapper: sc not available here\n");
  Thread *curr = current_thread();
  Entry_frame   *regs      = curr->regs();
  Syscall_frame *ipc_regs  = regs->syscall_frame();

  Mword entry_event_num    = (Mword)-1;
  Unsigned8 have_snd       = (ipc_regs->ref().op() & L4_obj_ref::Ipc_send)
                             || (ipc_regs->ref().op() == L4_obj_ref::Ipc_call);
  Utcb *utcb = curr->utcb().access(true);
  Task *curr_task = static_cast<Task*>(curr->space());
  int do_log = Jdb_ipc_trace::log()
               && Jdb_ipc_trace::check_restriction(curr->dbg_id(),
                                                   curr_task->dbg_id(),
                                                   ipc_regs, 0)
               && ipc_regs->tag().proto() != L4_msg_tag::Label_debugger;

  if (do_log)
    {
      Mword dbg_id;
	{
	  Obj_cap r = ipc_regs->ref();
          L4_fpage::Rights rights;
	  Kobject_iface *o = r.deref(&rights, true);
	  if (o)
	    dbg_id = o->dbg_info()->dbg_id();
	  else
	    dbg_id = ~0UL;
	}
      Tb_entry_ipc _local;
      Tb_entry_ipc *tb = EXPECT_TRUE(Jdb_ipc_trace::log_buf())
                       ? Jdb_tbuf::new_entry<Tb_entry_ipc>()
                       : &_local;
      //// TOMO: assumption about SC here!
      //tb->set(curr, regs->ip(), ipc_regs, utcb,
	    //  dbg_id, static_cast<Budget_sc *>(curr->get_sched_context())->get_left());
      tb->set(curr, regs->ip(), ipc_regs, utcb, dbg_id, 0);

      if (EXPECT_TRUE(Jdb_ipc_trace::log_buf()))
	Jdb_tbuf::commit_entry(tb);
      else
	Jdb_tbuf::direct_log_entry(tb, "IPC");

      entry_event_num = tb->number();
    }


  // now pass control to regular sys_ipc()
  sys_ipc_wrapper();

  if (Jdb_ipc_trace::log() && Jdb_ipc_trace::log_result() && do_log)
    {
      Tb_entry_ipc_res _local;
      Tb_entry_ipc_res *tb = static_cast<Tb_entry_ipc_res*>
	(EXPECT_TRUE(Jdb_ipc_trace::log_buf()) ? Jdb_tbuf::new_entry()
					    : &_local);
      tb->set(curr, regs->ip(), ipc_regs, utcb, utcb->error.raw(),
	      entry_event_num, have_snd, false);

      if (EXPECT_TRUE(Jdb_ipc_trace::log_buf()))
	Jdb_tbuf::commit_entry(tb);
      else
	Jdb_tbuf::direct_log_entry(tb, "IPC result");
    }
}
