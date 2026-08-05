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


(* $Id: astore.ml,v 1.3 2005/02/25 15:24:23 poirriez Exp $ *)

(**
 This module provides an adaptable length 
 non-persistent data structure.
 It is based upon arrays, thus it allows direct access.
*)

module type ASTORE =
  sig
    (** the data stored*)
    type 'elt t =
        { mutable tab: 'elt array array;
          size: int; 
          mutable size2: int;
          mutable nextini: int;
          mutable nextinj: int }
    val create : int -> int -> 'elt -> 'elt  t
        (**[create seg nbseg init] returns an evolmat of initial size [nbseg*seg],
           when extension occur, it will add [seg] cells
         *)
	
    val add : 'elt t -> 'elt -> int * int
        (**[add em elt] puts [elt] in [em], if necessary it extends [em], it returns
           the indexes of cell where elt is stored
         *)

    val get : 'elt t -> int * int -> 'elt
        (**[get em index] returns the elt stored at the given place*)

    val change : 'elt t -> int * int -> 'elt -> unit
        (**[change em index elt] puts [elt] in [em index] *)

    val next : 'elt t -> int * int 
        (** [next em] return the next inex where a data will be stored *)

    val reset : 'elt t -> unit
        (** [reset em] erases the data in [em] *)
  end

module M: ASTORE  = 
  struct
    type 'elt t ={ mutable tab : 'elt array array;
		   size : int;
		   mutable size2:int ;
		   mutable nextini : int;
		   mutable nextinj : int}
    let create i j init=
      let res = Array.create j [||] 
      in res.(0) <- Array.create i init ;
      {tab=res; size=i; size2=j; nextini = 0; nextinj=0}

    let next t = t.nextini,t.nextinj 
    let reset t = t.nextini <- 0; t.nextinj<-0
    let add t e =
      let i,j = t.nextini,t.nextinj in
      t.tab.(t.nextinj).(t.nextini) <- e;
      t.nextini <- t.nextini + 1;
      if t.nextini = t.size then
	begin
	  t.nextini <- 0;
	  t.nextinj <- t.nextinj+1;
	  if t.nextinj <  t.size2 then 
            t.tab.(t.nextinj) <- Array.create t.size t.tab.(0).(0)
	  else
            begin
              t.tab <- Array.append t.tab (Array.create 5 [||]);
              t.size2 <- t.size2 + 5;
              t.tab.(t.nextinj) <- Array.create t.size t.tab.(0).(0)
            end
	end;
      j,i

    let get t (j,i) = t.tab.(j).(i)

    let change t (j,i) e = t.tab.(j).(i) <- e
  end

