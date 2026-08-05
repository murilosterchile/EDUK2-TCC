(***************************************************************************)
(*                                  PYAsUKP                                *)
(*    PYAsUKP: Yet Another solver (for the) Unbounded Knapsack Problem     *)
(*           Vincent Poirriez with Nicola Yanev and Rumen Andonov          *)
(*                          LAMIH-ROI UMR CNRS 8530                        *)
(*  Copyright 1998-2005  Vincent Poirriez				   *)
(* <vincent Dot poirriez At univ-valenciennes Dot fr>                      *)
(*  This file is part of PYAsUKP.                                          *)
(*									   *)
(*  PYAsUKP is free software; you can redistribute it and/or modify        *)
(*  it under the terms of the GNU General Public License as published by   *)
(*    the Free Software Foundation; either version 2 of the License, or	   *)
(*    (at your option) any later version.				   *)
(*    PYAsUKP is distributed in the hope that it will be useful,	   *)
(*    but WITHOUT ANY WARRANTY; without even the implied warranty of	   *)
(*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the	   *)
(*    GNU General Public License for more details.			   *)
(*									   *)
(*  You should have received a copy of the GNU General Public License	   *)
(*  along with Foobar; if not, write to the Free Software		   *)
(*  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA*)
(***************************************************************************)



(* $Id: chainlist.mli,v 1.2 2005/02/25 15:24:23 poirriez Exp $ *)

(**This module implements explicitly chained lists*)

type 'a elt = { info : 'a; next : 'a rt; }
and 'a t = End | E of 'a elt
and 'a rt = 'a t ref

exception Empty

(** [Chainlist.create v] returns a new chained-list with  [v] as unique element *)
val create : 'a -> 'a rt

(** [Chainlist.is_empty cl] tests if [cl] is empty. *)
val is_empty : 'a rt -> bool

(** [Chainlist.delete v cl] deletes the first element in [cl] equals to [v],
    do nothing if non is found. *)
val delete : 'a -> 'a rt -> unit

(** [Chainlist.iter f cl] apply successively [f] on all the elements of [cl].*)
val iter : ('a -> 'b) -> 'a rt -> unit

(** [Chainlist.fold f init cl] 
    returns %$(f (..(f (f init v_1) v_2)..) v_n$% if %$v_1..v_n$ 
    are the values in [cl].*)
val fold : ('a -> 'b -> 'a) -> 'a -> 'b rt -> 'a

(** [Chainlist.insert_sorted ord cl v] inserts [v] at it's correct
 place in [cl], assuming that [cl] is sorted w.r.t. [ord]. *)
val insert_sorted : ('a -> 'a -> bool) -> 'a rt -> 'a -> unit

(** [Chainlist.put_in_sorted pred v cl] put [v] just before the first element
    [e] of [cl] such that [pred e = true]. If none is encountered,
    then [v] is put at the end of [cl]
*)
val put_in_sorted : ('a -> bool) -> 'a -> 'a rt -> unit

(** [Chainlist.insert_head cl v] put [v] at the beginning of [cl] *)
val insert_head : 'a rt -> 'a -> unit

(** [Chainlist.hd cl] returns the first element of [cl].
   Raises [Chainlist.Empty] if [cl] is empty.*)
val hd : 'a rt -> 'a

(** [Chainlist.take cl] returns the first element of [cl]
   and removes it from [cl].
   Raises [Chainlist.Empty] if [cl] is empty.*)
val take : 'a rt -> 'a

(** [Chainlist.is_single cl] returns [true] iff [cl] contains a unique value. *)
val is_single : 'a rt -> bool

(** [Chainlist.delete_cond_all pred cl] removes all from [cl] all its elements [v]
   such that [pred v = true]. *)
val delete_cond_all : ('a -> bool) -> 'a rt -> unit

(** [Chainlist.length cl] returns the number of elements in [cl] .*)
val length : 'a rt -> int

(** [Chainlist.get_equiv_heads pred cl] returns [(h,lh)] where
    [h] is the head of [cl] and [lh] the list of all the elements [e] immediatly
    following [h] such that [pred h e = true]. *)
val get_equiv_heads : ('a -> 'a -> bool) -> 'a rt -> 'a * 'a list

(** [Chainlist.tail_after pred cl] returns the end part of [cl] which starts
    with the first element [e] such that [pred e = true]. *)
val tail_after : ('a -> bool) -> 'a rt -> 'a rt

(** [Chainlist.find pred cl] returns  the first element [e] such that [pred e = true].
    Raises [Not_found] if none is encountered. *)
val find : ('a -> bool) -> 'a rt -> 'a
