/*
    Copyright (C) 2002 Paul Davis
    Copyright (C) 2003 Jack O'Quin
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.
    
    You should have received a copy of the GNU Lesser General Public License
    along with this program; if not, write to the Free Software 
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

    $Id$
*/

#ifndef __jack_transport_h__
#define __jack_transport_h__

#ifdef __cplusplus
extern "C" {
#endif

#include <jack/types.h>

/**
 * Transport states.
 */
typedef enum {

	/* the order matters for binary compatibility */
	JackTransportStopped = 0,	/**< Transport halted */
	JackTransportRolling = 1,	/**< Transport playing */
	JackTransportLooping = 2,	/**< For OLD_TRANSPORT, now ignored */
	JackTransportStarting = 3,	/**< Waiting for sync ready */

} jack_transport_state_t;

/**
 * Optional struct jack_position_t fields.
 */
typedef enum {

	JackPositionBBT =	0x10,	/**< Bar, Beat, Tick */

} jack_position_bits_t;

#define EXTENDED_TIME_INFO

/**
 * Struct for transport position information.
 */
typedef struct {
    
    /* these two cannot be set from clients: the server sets them */
    jack_time_t		usecs;		/**< monotonic, free-rolling */
    jack_nframes_t	frame_rate;	/**< current frame rate (per second) */

    jack_nframes_t	frame;		/**< frame number, always present */
    jack_position_bits_t valid;		/**< which other fields are valid */

    /* JackPositionBBT fields: */
    int		bar;			/**< current bar */
    int		beat;			/**< current beat-within-bar */
    int		tick;			/**< current tick-within-beat */
    double	bar_start_tick;            

    float	beats_per_bar;
    float	beat_type;
    double	ticks_per_beat;
    double	beats_per_minute;

    /* For binary compatibility, new fields should be allocated from
     * this padding area with new valid bits controlling access, so
     * the existing structure size and offsets are preserved. */
    int		padding[14];

    /* When (guard_usecs == usecs) the entire structure is consistent.
     * This is set by server. */
    jack_time_t    guard_usecs;		/**< guard copy of usecs */

} jack_position_t;

/**
 * Called by the timebase master to release itself from that
 * responsibility.
 *
 * If the timebase master releases the timebase or leaves the JACK
 * graph for any reason, the JACK engine takes over at the start of
 * the next process cycle.  The transport state does not change.  If
 * rolling, it continues to play, with frame numbers as the only
 * available position information.
 *
 * @see jack_set_timebase_callback
 *
 * @param client the JACK client structure.
 *
 * @return 0 on success, otherwise a non-zero error code.
 */
int  jack_release_timebase (jack_client_t *client);

/**
 * Prototype for the sync callback defined by slow-sync clients.
 * Called just before process() in the same thread when some client
 * has requested a new position.  This realtime function must not
 * wait.
 *
 * The transport @a state will be:
 *
 *   - ::JackTransportStopped some client just requested a new position;
 *   - ::JackTransportStarting the transport is waiting to start;
 *   - ::JackTransportRolling the timeout has expired, and the position
 *   is now a moving target.
 *
 * @param state current transport state.
 * @param pos new transport position.
 * @param arg the argument supplied by jack_set_sync_callback().
 *
 * @return true (non-zero) when ready to roll.
 */ 
typedef int  (*JackSyncCallback)(jack_transport_state_t state,
				 jack_position_t *pos,
				 void *arg);

/**
 * Register (or unregister) as a slow-sync client, one that cannot
 * respond immediately to transport position changes.
 *
 * When there are slow-sync clients and the JACK transport starts
 * playing in a new postion, it first enters the
 * ::JackTransportStarting state.  The @a sync_callback function for
 * each slow-sync client will be invoked in the JACK process thread
 * while the transport is starting.  If it has not already done so,
 * the client needs to initiate a seek to reach the starting position.
 * The @a sync_callback returns false until the seek completes and the
 * client is ready to play.  When all slow-sync clients are ready, the
 * state changes to ::JackTransportRolling.
 *
 * Clients that don't set a @a sync_callback are assumed to be ready
 * immediately, any time the transport wants to start.
 *
 * @param client the JACK client structure.
 * @param sync_callback is a realtime function that returns true when
 * the client is ready.  Setting @a sync_callback to NULL declares that
 * this client no longer requires slow-sync processing.
 * @param arg an argument for the @a sync_callback function.
 *
 * @return 0 on success, otherwise a non-zero error code.
 */
int  jack_set_sync_callback (jack_client_t *client,
			     JackSyncCallback sync_callback,
			     void *arg);

/**
 * Set the timeout value for slow-sync clients.
 *
 * This timeout prevents unresponsive slow-sync clients from
 * completely halting the transport mechanism.  The default is two
 * seconds.  When the timeout expires, the transport starts rolling,
 * even if some slow-sync clients are still unready.  The
 * sync_callbacksof these clients continue being invoked, giving them
 * a chance to catch up.
 *
 * @see jack_set_sync_callback
 *
 * @param client the JACK client structure.
 * @param timeout is delay (in frames) before the timeout expires.
 *
 * @return 0 on success, otherwise a non-zero error code.
 */
int  jack_set_sync_timeout (jack_client_t *client,
			    jack_nframes_t timeout);

/**
 * Prototype for the timebase master callback used to continuously
 * update position information.  Without extended information, there
 * is no need for this function, JACK can count frames automatically.
 *
 * This function is called immediately after process() in the same
 * thread whenever the transport is rolling, or when any client has
 * set a new position in the previous cycle.  The first cycle after
 * jack_set_timebase_callback() is also treated as a new position.
 * This realtime function must not wait.  Its output affects all of
 * the following process cycle, unless a new position request arrives
 * during the current cycle.
 *
 * The timebase master should not use its @a pos argument to set a
 * discontinuous position.  Use jack_transport_reposition() or
 * jack_transport_goto_frame(), instead.
 *
 * @param state current transport state.
 * @param nframes number of frames in current period.
 * @param pos pointer to a structure containing the current position.
 * Store updated position information for the next cycle here.  At a
 * minimum, @a pos->frame must be incremented.
 * @param new_pos true (non-zero) for a newly requested @a pos, or
 * for the first cycle after jack_set_timebase_callback().
 * @param arg the argument supplied by jack_set_timebase_callback().
 */ 
typedef void (*JackTimebaseCallback)(jack_transport_state_t state,
				     jack_nframes_t nframes, 
				     jack_position_t *pos,
				     int new_pos,
				     void *arg);

/**
 * Register as timebase master for the JACK subsystem.
 *
 * The timebase master is responsible for counting beats, frames,
 * etc. on every process cycle.  There is never more than one at a
 * time.  The timebase master registers a callback that updates
 * position information while the transport is rolling.
 *
 * When a new client takes over, the former timebase callback is no
 * longer called.  Taking over the timebase may be done conditionally,
 * so it fails if there is a master already.
 *
 * @param client the JACK client structure.
 * @param conditional non-zero for a conditional request.
 * @param timebase_callback is a realtime function that returns
 * position information.
 * @param arg an argument for the @a timebase_callback function.
 *
 * @return
 *   - 0 on success;
 *   - EBUSY if a conditional request fails because there is already a
 *   timebase master;
 *   - other non-zero error code.
 */
int  jack_set_timebase_callback (jack_client_t *client,
				 int conditional,
				 JackTimebaseCallback timebase_callback,
				 void *arg);

/**
 * Reposition transport to a new frame number.
 *
 * May be called at any time by any client.  If valid, the new
 * position takes effect in the next process cycle.  The timebase
 * master and any slow-sync clients are notified of the new position.
 * If there are slow-sync clients and the transport is already
 * rolling, it enters the ::JackTransportStarting state and begins
 * invoking their sync_callbacks until they declare themselves ready.
 *
 * @see jack_transport_reposition, jack_set_sync_callback
 * 
 * @param client the JACK client structure.
 * @param frame frame number of new transport position.
 *
 * @return 0 if valid request, otherwise a non-zero error code.
 */
int  jack_transport_goto_frame (jack_client_t *client,
				jack_nframes_t frame);

/**
 * Query the current transport state and position.
 *
 * This function can be called from any thread.  If called from the
 * process thread, @a pos corresponds to the first frame of the
 * current cycle and the state returned is valid for the entire cycle.
 *
 * @param client the JACK client structure.
 * @param pos current position structure, @a pos->valid describes
 * which fields contain valid data.
 *
 * @return Current transport state.
 */
jack_transport_state_t jack_transport_query (jack_client_t *client,
					     jack_position_t *pos);

/**
 * Request a new transport position.
 *
 * May be called at any time by any client.  If valid, the new
 * position takes effect in the next process cycle.  The timebase
 * master and any slow-sync clients are notified of the new position.
 * If there are slow-sync clients and the transport is already
 * rolling, it enters the ::JackTransportStarting state and begins
 * invoking their sync_callbacks until they declare themselves ready.
 *
 * @see jack_transport_goto_frame, jack_set_sync_callback
 * 
 * @param client the JACK client structure.
 * @param pos requested new transport position.
 *
 * @return 0 if valid request, otherwise a non-zero error code.
 */
int  jack_transport_reposition (jack_client_t *client,
				jack_position_t *pos);

/**
 * Start the JACK transport rolling.
 *
 * Any client can make this request at any time.  It takes effect no
 * sooner than the next process cycle, perhaps later if there are
 * slow-sync clients.
 *
 * @see jack_set_sync_callback
 *
 * @param client the JACK client structure.
 */
void jack_transport_start (jack_client_t *client);

/**
 * Stop the JACK transport.
 *
 * Any client can make this request at any time.  It takes effect on
 * the next process cycle.
 *
 * @param client the JACK client structure.
 */
void jack_transport_stop (jack_client_t *client);


/*********************************************************************
 * The following interfaces are DEPRECATED.  They are only provided
 * for compatibility with the earlier JACK transport implementation.
 *********************************************************************/

/**
 * Optional struct jack_transport_info_t fields.
 *
 * @see jack_position_bits_t.
 */
typedef enum {

	JackTransportState =    0x1,	/**< Transport state */
	JackTransportPosition = 0x2,	/**< Frame number */
	JackTransportLoop =     0x4,	/**< Loop boundaries (ignored) */
	JackTransportSMPTE =    0x8,	/**< SMPTE (ignored) */
	JackTransportBBT =      0x10,	/**< Bar, Beat, Tick */

} jack_transport_bits_t;

/**
 * Deprecated struct for transport position information.
 *
 * @deprecated This is for compatibility with the earlier transport
 * interface.  Use the jack_position_t struct, instead.
 */
typedef struct {
    
    /* these two cannot be set from clients: the server sets them */

    jack_nframes_t frame_rate;		/**< current frame rate (per second) */
    jack_time_t    usecs;		/**< monotonic, free-rolling */

    jack_transport_bits_t  valid;	/**< which fields are legal to read */
    jack_transport_state_t transport_state;         
    jack_nframes_t         frame;
    jack_nframes_t         loop_start;
    jack_nframes_t         loop_end;

    long           smpte_offset;	/**< SMPTE offset (from frame 0) */
    float          smpte_frame_rate;	/**< 29.97, 30, 24 etc. */

    int            bar;
    int            beat;
    int            tick;
    double         bar_start_tick;            

    float          beats_per_bar;
    float          beat_type;
    double         ticks_per_beat;
    double         beats_per_minute;

} jack_transport_info_t;
	
/**
 * Gets the current transport info structure (deprecated).
 *
 * @param client the JACK client structure.
 * @param tinfo current transport info structure.  The "valid" field
 * describes which fields contain valid data.
 *
 * @deprecated This is for compatibility with the earlier transport
 * interface.  Use jack_transport_query(), instead.
 *
 * @pre Must be called from the process thread.
 */
void jack_get_transport_info (jack_client_t *client,
			      jack_transport_info_t *tinfo);

/**
 * Set the transport info structure (deprecated).
 *
 * @param client the JACK client structure.
 * @param tinfo used to return a new transport info structure used
 * for the next process cycle.  The "valid" field must say which
 * fields contain valid data.
 *
 * @deprecated This is for compatibility with the earlier transport
 * interface.  Define a ::JackTimebaseCallback, instead.
 *
 * @pre Caller must be the current timebase master.  Must be called
 * from the process thread.
 */
void jack_set_transport_info (jack_client_t *client,
			      jack_transport_info_t *tinfo);

#ifdef __cplusplus
}
#endif

#endif /* __jack_transport_h__ */
