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


(*$Id: timing.ml,v 1.2 2005/02/28 10:39:35 poirriez Exp $*)
(*      timing.ml     *)
(** Pour calculer les temps de calcul *)
(*une fonction generale *)
open Unix
let dif_time {tms_utime=ut1;tms_stime=st1;tms_cutime=cut1;tms_cstime=cst1} 
             {tms_utime=ut2;tms_stime=st2;tms_cutime=cut2;tms_cstime=cst2}=
{tms_utime=ut2-.ut1;tms_stime=st2-.st1;tms_cutime=cut2-.cut1;tms_cstime=cst2-.cst1};;
let all_timing f e =
 let t1 = times() in
 let v = f e in
 let t2 =times() in
 (dif_time t1 t2,v);;
let timing f e =
let t,v=all_timing f e in (t.tms_utime),v;;
(*let timer f e =
 let t1 = time() in
 let v = f e in
 let t2 =time() in
 (t2- t1,v);;
*)
