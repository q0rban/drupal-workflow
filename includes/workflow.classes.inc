<?php

// $Id:$

/**
 * @file
 * Workflow OO structure and accompanying API.
 */
class Workflow {
  var $wid;
  var $name;
  var $label;
  var $roles;
  var $states;
  var $options;
}

class WorkflowState() {
  var $sid;
  var $name;
  var $label;
  var $weight;
  var $status;
}