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



(* $Id: edukio.ml,v 1.2 2005/02/28 10:39:35 poirriez Exp $ *)
(*edukio.ml*)


open Wandp.M
let rec blanc_prec s i =
  if i= -1 or s.[i]=' ' or s.[i]='\t' then i else blanc_prec s (i-1)
let read_vect ch =
  let s =  try input_line ch with End_of_file -> "" in
  if s = "" then [||]
  else
    let rec explode i res =
      if i<0 then res else
      let j = blanc_prec s i in explode (j-1) ((String.sub s (j+1) (i-j))::res)
    in
    Array.of_list(explode (String.length s -1) [])

exception Bad_Format
let items_of_file file =
  try 
    let ch = open_in file in
    begin
      let buffer = ref (read_vect ch) in
      let continue = ref true in
      let n =
        while !continue do
          if Array.length !buffer = 0 then buffer:= read_vect ch
          else
            (match !buffer.(0).[0] with
              '#'|'%' -> buffer:= read_vect ch
            | 'n' | 'm' -> continue := false
            | _    -> raise Bad_Format)
        done;
        int_of_string !buffer.(1)
      in
      continue:= true;
      let rw = Array.create (n+1) zerow in
      let rp = Array.create (n+1) zerop in
      buffer:= read_vect ch;
      let c =
	while !continue do
          if Array.length !buffer = 0 then  buffer:= read_vect ch
          else
            (match !buffer.(0).[0] with
              '#'|'%' -> buffer:= read_vect ch
            | 'c'  -> continue := false
            | _    -> raise Bad_Format)
	done;
        weight_of_string (!buffer.(1)) in
      continue:= true;
      buffer:= read_vect ch;
      while !continue do
        match Array.length !buffer with
          0 -> buffer:= read_vect ch
        | _ ->
            (match !buffer.(0).[0] with
              '#'|'%' -> buffer:= read_vect ch
            | 'b'  ->continue := false
            | _    -> raise Bad_Format)
      done;
      continue := true;
      for i = 1 to n do
        buffer:= read_vect ch;
        rw.(i) <- weight_of_string !buffer.(0);
        rp.(i) <- profit_of_string !buffer.(1)
      done;
      close_in ch;
      rw,rp,n,c
    end
  with e -> (prerr_endline "Erreur dans get_data"; raise e)

let put_data file comment n c w p =
  try
    let ch = open_out file in 
    output_string ch ("##\n");
    output_string ch ("##"^comment^"\n\n");
    output_string ch ("n: "^(string_of_int n)^"\n\n");
    output_string ch ("c: ");
    output_string ch ((string_of_weight c)^(String.make 30 ' ')^"\n\n");
    output_string ch ("begin data \n");
    for i = 1 to n do
      output_string ch ((string_of_weight w.(i))^"\t"^(string_of_profit p.(i))^"\n")
    done;
    output_string ch ("end data \n");
    flush ch;
    close_out ch
  with e -> (prerr_endline "Error in put_data";raise e)
