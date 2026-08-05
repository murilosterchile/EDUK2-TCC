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



(* $Id: edukio.mli,v 1.2 2005/02/28 10:39:35 poirriez Exp $ *)

(** Here are provided tools to read from data files and to
    write in data files.*)

exception Bad_Format

(** [item_of_file fname] returns [(w,p,n,c)] the
    arrays of weight and profit, the number of items and the capacity read in
    the file of name [fname].
    The format of the file has to be:

##These are 
##lines of comments

n: 7

c: 2900

## In the first column are the weight, in the second the profits
begin data 
120	300
245	580
130	301
260	601
310	605
194	322
190	310
end data 

#every thing after the end data line is meaning less.

*)
val items_of_file : string -> Wandp.M.w array * Wandp.M.p array * int * Wandp.M.w

(** [put_data fname comment n c w p] create the file
   [fname] and write it with  the correct format.
   The [comment] is written in the second line.*)
val put_data :
  string -> string -> int -> Wandp.M.w -> Wandp.M.w array -> Wandp.M.p array -> unit
