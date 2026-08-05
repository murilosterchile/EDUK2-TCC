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



(* $Id: datastructtype.ml,v 1.4 2005/04/27 13:09:32 poirriez Exp $ *)
(*datastructtype.ml*)

open Wandp.M
open Globals
open Sequences

(** The hybrid algorithm computes upper bounds, so it may occur that a computed
   solution appears to be equal to the upper bound. In this case the 
   computation is stopped and the optimal solution is the upper bound.
   To take in account this ability, the type [end_of_computation] is defined. 
*)

(** This module defines the bounds type stuff. We use the notations of
   Martello and Toth (in {% \cite\{MT90\}%} section 3.6.1 p 92-93), with the following
  naming scheme, for any identifier {% $a$, $\overline\{a\}$%} is replaced with {% $ab$%}. 
   The indexes 1, 2, 3 refer to the three best items in term of the ration 
   [profit / weight].
*)
module BOUNDS = 
  struct
    type sort = 
      | Smt (** to compute only MT bounds *)
      | Sv  (** to compute the new bound *) 
      | Sboth (** to compute both bounds *) 
      | Suphalfc (** In the case we stop at c/2 *)
    type mt_misc = {
	xb_2 : int ;  (** the maximal number of item 2 in [cb]   *)
	xb_3 : int ;  (** the maximal number of item 3 to obtain in [c'] *)
	mtb2 : item;  (** the  second best item *)
	mtb3 : item;  (** the  third best item *)
      } 
    type v_misc = {
	imin : item;  (** the minimal wheighted item *)
	delta_1 : p;  (** the difference [imin.p - imin.w] *)
	psi : int;    (** the smallest int s.t. [psi*imin.p > imin.w] *)
	alpha : float;  
      } 
    type both = {mt : mt_misc; v : v_misc; mutable sb: sort}
    type u_misc = {mutable cp : Wandp.M.cp;mutable stbpt: int*int}
    type misc = MT of mt_misc | V of v_misc | Both of both | Uphc of u_misc
    type t =
	{ c :  w;      (** the capacity to fill *)
	  u : p;       (** The upper bound *)
	  z : p ref;   (** The best known solution. *)
	  b1 : item;   (** The best item type *)
	  xb_1 : int ; (** the maximal number of item 1 in [c]  *)
	  mutable misc : misc; (** Auxiliary data need to compute the bound *)
    } 

    exception Optimal of t * ( w * p * (int * int)) * ((int*int) list)

    let (rbound: t option ref) = ref None    
    let messbigint = ref true    
    let to_string b =
       Printf.sprintf "c: %d;u: %d; z: %d; b1:(%d,%d,%f);xb_1:%d" b.c b.u !(b.z) b.b1.w b.b1.p b.b1.r b.xb_1
  end 

(** To catch the value at the end of the computation.*)
type end_of_computation = 
  | With_bound of ( BOUNDS.t * (w * p * (int * int)) * ((int * int) list))
	(** (wpt,ppt) is the critical point over which the bound is computed *)
  | Normal of 
      ( bool * (** to say if the period level is reached *)
          cp *     (** the optimal solution: (optimal weight, optimal profit)*)
          (int * int) *  (** Index of the best item in term of ratio profit/weight *)
          int *  (** The number of article of the best type above to be used to
                    fill the capacity above the periodic level*)
          (int * int) * 
          (** The starting point of bactrack within the sequence of 
             optimal critical points*)
          w *    (** The last capacity computed *)
          w      (** The capacity of the end of the reduction phase *)
       )

(** Informations about the item.*)        
type iteminfos = { 
    last_contribution : Wandp.M.w  Astore.M.t; 
    (** The last capacity for which the item was the last one to contribute.*)
    nb_last_contribution_alone : int Astore.M.t; 
    (** The greatest past nb such that for [nb*w.k], the item is the only ressource used
       to build the optimal solution.*)
    item :  int Astore.M.t; (** The item *)
    ratio :  r Astore.M.t;  (** The item ratio *)
    next_built_upon :  Seq.index ref Astore.M.t; 
    (** The reference of the index of the critical point in [sequence_result]
       upon which build the next contribution of the item.*)
  } 

