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

(* $Id: sequences.mli,v 1.2 2005/02/28 10:39:35 poirriez Exp $*)

module type SEQ =
  sig
    type elt1 = Wandp.M.w
    type elt2 = Wandp.M.p
    type elt3 = int * int
    type elt = elt1 * elt2 * elt3
    type index = int * int
    type t
    val zero_index : index
    val length : t -> int
    val sizemax2 : t -> int
    val create : int -> int -> t
    val reset : t -> unit
    val resetout : t -> unit
    val put : t -> elt -> unit
    val put_replace : t -> elt -> unit
    val get : t -> elt
    val getdown : t -> elt
    val peek : t -> elt
    val peek1 : t -> elt1
    val junk : t -> unit
    val lastval : t -> elt
    val lastin : t -> index
    val jumpin : t -> index -> unit
    val jumpout : t -> index -> unit
    val iter : (elt1 -> elt2 -> elt3 -> 'a) -> t -> unit
    val isempty : t -> bool
    val item_in : t -> index
    val item_out : t -> index
    val print :
      (elt1 -> unit) -> (elt2 -> unit) -> (elt3 -> unit) -> t -> unit
    val  print_seq :  t -> unit
    val  print_seq_from :  t -> int * int -> unit
    val search_down1_from : (elt1 -> bool) -> t -> index -> elt * index
    val search_down1 : (elt1 -> bool) -> t -> elt * index
    val search_max_two_ways :
	t-> Wandp.M.w -> (elt * index ) -> elt -> index * index * Wandp.M.p
  end
module Seq : (SEQ with type elt1 = Wandp.M.w)
