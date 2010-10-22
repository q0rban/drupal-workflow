<?php

// $Id:$

/**
 * @file
 * Workflow OO structure and accompanying API.
 */
 
/**
* Workflow State class
*/
class workflow_state {
  
  var $sid;
  var $name;
  var $label;
  
  var $weight;
  var $sysid;
  var $status;
  
}

/**
* Workflow class
*/
class workflow {
  
  var $wid;
  var $name;
  var $label;
  
  var $roles;
  var $states;
  var $node_types;
  var $options;
  
  function __construct($name = NULL) {
    
    // Pre-populate the object if name was passed to the constructor
    if (!empty($name)) {
      $this->name = $name;
      $obj_prop = self::query_workflow($name);
      $this->wid = $obj_prop->wid;
      $this->label = $obj_prop->label;
      $this->options = $obj_prop->options;      
      
      $this->tab_roles = $obj_prop->tab_roles;      
      $this->roles = self::tab_roles_to_array($this->tab_roles);
      
      $this->states = self::query_states($name);
      $this->node_types = self::query_nodetypes($name);
    }
    
  }

  static function tab_roles_to_array($tab_roles) {
      // If no roles are set yet, return empty array.
      if(empty($tab_roles)) {
        return array();
      }
      // If it's a single value return a single-value array.
      else if(strpos($tab_roles, ",") === FALSE) {
        return array($tab_roles);
      }
      // If none of the above, we should have a comma-separated list
      else {
        return explode(",", $tab_roles);
      }      
  }


  /**
  * Query workflows table in the database
  *
  * @param $workflow_name
  *   The machine name of the workflow
  *
  * @return
  *   The workflow object with data populated just from the "workflows" table.
  */
  static function query_workflow($workflow_name) {
    if (empty($workflow_name)) {
      return array();
    }
    
    $result = db_query("SELECT * FROM {workflows} WHERE name = '%s'", $workflow_name);
    
    // $name is a unique field. We can only get one result
    $row = db_fetch_typed_object($result, 'workflow');
    $row->options = unserialize($row->options);
    return $row;
  }
  
  /**
  * Query database for the states associated with the current workflow 
  *
  * @param $workflow_name
  *   The machine name of the workflow
  *
  * @return
  *   The array of state objects from the database.
  */
  static function query_states($workflow_name) {    
    if (empty($workflow_name)) {
      return array();
    }
    
    
    if (!empty(self::$states_cache[$workflow_name])) {
      return self::$states_cache[$workflow_name];
    }

    
    $result = db_query("SELECT * FROM {workflow_states} WHERE workflow_name = '%s'", $workflow_name);
    
    $states = array();
    while ($row = db_fetch_typed_object($result, 'workflow_state')) {
      //$row->weight = (int) $row->weight;
      $states[] = $row;
    }

    self::$states_cache[$workflow_name] = $states;    
    
    return $states;
  }
  static $states_cache = array();   
  
  /**
  * Query database for the node types associated with the current workflow 
  *
  * @param $workflow_name
  *   The machine name of the workflow
  *
  * @return
  *   The array of node_type names associated with the workflow from the database.  
  */
  static function query_nodetypes($workflow_name) {
    if (empty($workflow_name)) {
      return array();
    }

    //static $node_types;    
    if (!empty(self::$node_types_cache[$workflow_name])) {
      return self::$node_types_cache[$workflow_name];
    }
    
    $result = db_query("SELECT `type` FROM {workflow_type_map} WHERE workflow_name = '%s'", $workflow_name);
    
    $types = array();
    while ($row = db_fetch_object($result)) {
      $types[] = $row->type;
    }
    
    self::$node_types_cache[$workflow_name] = $types;
    return $types;
  }
  static $node_types_cache = array();
  
}

function workflow_export_callback($workflow, $prefix = '') {

  $workflow->states = workflow::query_states($workflow->name);    
  $workflow->node_types = workflow::query_nodetypes($workflow->name);  
    
    
  ctools_include('export');
  $output = ctools_export_object('workflows', $workflow, $prefix);
  
  $output .= "$prefix" . '$workflow->states = array();' . "\n";  
  if (is_array($workflow->states)) {
    $counter = 0;
    foreach ($workflow->states as $state) {
      $output .= "\n";
      $output .= ctools_export_object('workflow_states', $state, $prefix);
      $output .= "$prefix" . '$workflow->states[' . $counter . '] = $workflow_state' . ";\n";
      $counter++;      
    }
    $output .= "\n";
  }
  
  $roles = workflow::tab_roles_to_array($workflow->tab_roles);  
  
  $output .= "$prefix" . '$workflow->roles = ' . ctools_var_export($roles, $prefix) . ";\n";
  $output .= "$prefix" . '$workflow->node_types = ' . ctools_var_export($workflow->node_types, $prefix) . ";\n";  
  
  return $output;
  
}

/**
* Tinyints are interpreted as booleans by CTools. To avoid that we need custom exporter (or we'd have to change field type).
*/
function workflow_state_weight_exporter($workflow, $field, $value, $indent) {
  if ($field == 'weight') {
    return (int) $workflow->weight;
  }
}


/*
  Kind of code we need to generate:
  
  $workflow = new workflow;
  $workflow->disabled = FALSE; 
  $workflow->api_version = 1;
  $workflow->wid = '1';
  $workflow->name = 'op_basic_publishing_workflow';
  $workflow->label = 'Basic Publishing Workflow';
  $workflow->tab_roles = '';
  $workflow->options = array(
    'comment_log_node' => 1,
    'comment_log_tab' => 1,
  );
  
*/




/**
 * While db_fetch_object in Drupal is just a delegator to DB-specific fetch_object() functions, the original ones can
 * load a resultset in a class-typed object. Unfortunately, Drupal always puts stuff on stdClass. This function bypasses
 * the limitation.
 *
 * @param $resultset
 *   DB resultset resource
 *
 * @param $classname
 *   Class name to type-cast the DB row to.
 *
 * @return
 *   type-casted object
 */
function db_fetch_typed_object($resultset, $classname) {
  global $db_type;
  
  switch ($db_type) {
    case 'mysqli':
      $object = mysqli_fetch_object($resultset, $classname);
      return isset($object) ? $object : FALSE;
    case 'mysql':
      return mysql_fetch_object($resultset, $classname);
    case 'pgsql':
      return pg_fetch_object($resultset, NULL, $classname);    
    default:
      //-- Sensible default, but the object will be of stdClass type, so may not fully support logic in other places.
      return db_fetch_object($resultset);
  }
}
